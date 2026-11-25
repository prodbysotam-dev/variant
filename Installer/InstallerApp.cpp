// Installateur Windows de Variant : un seul .exe qui contient les plugins (zip collé à la fin du fichier).
// - repère les logiciels de musique installés (FL Studio, Ableton, Cubase, Studio One, Reaper, Bitwig…)
// - met les .vst3 dans le dossier VST3 que ces logiciels lisent, + leurs dossiers de plugins perso (FL, Reaper),
//   + un dossier au choix
// - copie la bibliothèque de sons livrée à côté (où l'utilisateur veut) et note son emplacement pour Variant
// Les anciens effets renommés (Variant OTT, Portal…) sont mis de côté pour ne pas apparaître en double.
#include <juce_gui_basics/juce_gui_basics.h>
#include <windows.h>

namespace
{
    const juce::Colour bg (0xff15151b), panel (0xff1f1f28), text (0xffeeeef4), dim (0xff9a9aae), accent (0xff8fb8ff), good (0xff8fe3b0), bad (0xffff8a8a), warn (0xffffc98a);
    juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }

    // zip collé à la fin de l'installateur : [exe][zip][taille du zip sur 8 octets]["VARIANTZ"]
    std::unique_ptr<juce::InputStream> payload()
    {
        const auto self = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
        auto in = std::make_unique<juce::FileInputStream> (self);
        if (! in->openedOk() || in->getTotalLength() < 16) return {};
        const auto total = in->getTotalLength();
        in->setPosition (total - 16);
        char magic[9] = {};
        const auto len = in->readInt64();
        in->read (magic, 8);
        if (juce::String (magic) != "VARIANTZ" || len <= 0 || len > total - 16) return {};
        return std::make_unique<juce::SubregionStream> (in.release(), total - 16 - len, len, true);
    }

    juce::File standardVst3()
    {
        return juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory).getChildFile ("Common Files").getChildFile ("VST3");
    }

    // noms des effets avant leur renommage (pour ne pas les avoir en double dans le DAW)
    juce::StringArray oldNames()
    {
        return { "Variant OTT", "Variant Pression", "Variant Portal", "Variant Nuage", "Variant Crystal", "Variant Halftime", "Variant Gross",
                 "Variant Decoupe", "Variant Detraqueur", "Variant Chroma", "Variant Liquide", "Variant Remous" };
    }

    juce::StringArray listPlugins()
    {
        juce::StringArray seen;
        if (auto in = payload())
        {
            juce::ZipFile zip (in.release(), true);
            for (int i = 0; i < zip.getNumEntries(); ++i)
            {
                const auto top = zip.getEntry (i)->filename.replaceCharacter ('\\', '/').upToFirstOccurrenceOf ("/", false, false);
                if (top.endsWithIgnoreCase (".vst3")) seen.addIfNotAlreadyThere (top.dropLastCharacters (5));
            }
        }
        return seen;
    }

    //==========================================================================
    // Logiciels de musique installés
    struct Daw { juce::String name; bool ok; juce::String note; };

    std::vector<Daw> detectDaws()
    {
        std::vector<Daw> out;
        const auto pf = juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory);
        const juce::File pd ("C:\\ProgramData");
        auto scan = [&] (const juce::File& dir, const juce::String& wildcard, const juce::String& label, bool ok, const juce::String& note)
        {
            if (! dir.isDirectory()) return;
            auto found = dir.findChildFiles (juce::File::findDirectories, false, wildcard);
            found.removeIf ([] (const juce::File& f) { return f.getFileName().containsIgnoreCase ("ASIO"); });
            if (found.isEmpty()) return;
            juce::String n = label.isNotEmpty() ? label : found.getLast().getFileName();
            for (auto& d : out) if (d.name == n) return;
            out.push_back ({ n, ok, note });
        };
        const auto std3 = u8 ("lit le dossier VST3 standard");
        scan (pf.getChildFile ("Image-Line"), "FL Studio*", "FL Studio", true, std3 + u8 (" (+ tes dossiers perso)"));
        scan (pd.getChildFile ("Ableton"), "Live*", "Ableton Live", true, std3);
        scan (pf.getChildFile ("Ableton"), "Live*", "Ableton Live", true, std3);
        scan (pf.getChildFile ("Steinberg"), "Cubase*", "Cubase", true, std3);
        scan (pf.getChildFile ("Steinberg"), "Nuendo*", "Nuendo", true, std3);
        scan (pf.getChildFile ("PreSonus"), "Studio One*", "Studio One", true, std3);
        scan (pf.getChildFile ("Fender"), "Studio Pro*", "Studio Pro", true, std3);
        scan (pf, "REAPER*", "REAPER", true, std3 + u8 (" (+ tes dossiers perso)"));
        scan (pf, "Bitwig Studio*", "Bitwig Studio", true, std3);
        scan (pf, "Cakewalk*", "Cakewalk", true, std3);
        scan (pf, "Tracktion*", "Waveform", true, std3);
        scan (pf.getChildFile ("Avid"), "Pro Tools*", "Pro Tools", false, u8 ("n'ouvre que les plugins AAX : Variant ne peut pas y être utilisé"));
        return out;
    }

    // dossiers de plugins perso réglés dans FL Studio et REAPER (en plus du dossier standard)
    juce::Array<juce::File> customVst3Folders()
    {
        juce::Array<juce::File> out;
        auto consider = [&] (juce::String p)
        {
            p = p.trim().unquoted();
            if (p.isEmpty() || p.contains ("%")) return;
            const juce::File d (p);
            if (! d.isDirectory() || d == standardVst3()) return;
            const auto n = d.getFullPathName().toLowerCase();
            const bool vst3ish = n.contains ("vst3") || ! d.findChildFiles (juce::File::findFilesAndDirectories, false, "*.vst3").isEmpty();
            // pas les dossiers par défaut de FL (VST2, 32 bits, CLAP) : seulement de vrais dossiers VST3 perso
            const bool legacy = n.contains ("(x86)") || n.contains ("common files") || n.contains ("vstplugins") || n.contains ("vst2") || n.contains ("clap");
            if (vst3ish && ! legacy) out.addIfNotAlreadyThere (d);
        };
        for (int i = 0; i < 64; ++i)   // FL Studio : Options > File settings > dossiers de recherche
        {
            const auto key = "HKEY_CURRENT_USER\\Software\\Image-Line\\Shared\\Plugin search paths\\List\\" + juce::String (i) + "\\path";
            if (! juce::WindowsRegistry::valueExists (key)) { if (i > 20) break; continue; }
            consider (juce::WindowsRegistry::getValue (key));
        }
        const auto ini = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("REAPER").getChildFile ("reaper.ini");
        juce::StringArray lines; lines.addLines (ini.loadFileAsString());
        for (auto& l : lines)
            if (l.startsWithIgnoreCase ("vstpath64=") || l.startsWithIgnoreCase ("vstpath="))
                for (auto& p : juce::StringArray::fromTokens (l.fromFirstOccurrenceOf ("=", false, false), ";", ""))
                    consider (p);
        return out;
    }

    //==========================================================================
    // chemins longs (plus de 260 caractères, ex. dossier décompressé très loin) : préfixe \\?\ de Windows
    std::wstring longPath (const juce::File& f)
    {
        const juce::String bs = juce::String::charToString ('\\');
        auto p = f.getFullPathName();
        if (p.startsWith (bs + bs)) p = bs + bs + "?" + bs + "UNC" + bs + p.substring (2);
        else p = bs + bs + "?" + bs + p;
        return std::wstring (p.toWideCharPointer());
    }
    juce::int64 sizeLong (const juce::File& f)
    {
        WIN32_FILE_ATTRIBUTE_DATA a;
        if (! GetFileAttributesExW (longPath (f).c_str(), GetFileExInfoStandard, &a) || (a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return -1;
        return ((juce::int64) a.nFileSizeHigh << 32) | a.nFileSizeLow;
    }
    void makeDirLong (const juce::File& d)
    {
        if (d.isRoot() || d == juce::File()) return;
        const auto w = longPath (d);
        const auto at = GetFileAttributesW (w.c_str());
        if (at != INVALID_FILE_ATTRIBUTES && (at & FILE_ATTRIBUTE_DIRECTORY)) return;
        makeDirLong (d.getParentDirectory());
        CreateDirectoryW (w.c_str(), nullptr);
    }
    bool copyLong (const juce::File& from, const juce::File& to)
    {
        makeDirLong (to.getParentDirectory());
        return CopyFileW (longPath (from).c_str(), longPath (to).c_str(), FALSE) != 0;
    }

    //==========================================================================
    // la bibliothèque de sons livrée à côté de l'installateur
    juce::File libraryPackage()
    {
        const auto here = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
        for (auto base : { here, here.getParentDirectory() })
            for (auto& d : base.findChildFiles (juce::File::findDirectories, false, "Variant Biblioth*"))
                return d;
        return {};
    }

    // où Variant garde ses réglages (l'emplacement de la bibliothèque y est noté)
    juce::File& appDataOverride() { static juce::File f; return f; }
    juce::File variantDataDir()
    {
        const auto base = appDataOverride() != juce::File() ? appDataOverride() : juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
        return base.getChildFile ("Variant");
    }

    juce::String copyLibrary (const juce::File& dst, const std::function<void (int)>& progress)
    {
        const auto src = libraryPackage();
        if (src == juce::File()) return {};   // installateur sans bibliothèque
        if (src == dst) return {};
        const auto files = src.findChildFiles (juce::File::findFiles, true, "*");
        juce::int64 total = 0, done = 0;
        for (auto& f : files) total += juce::jmax ((juce::int64) 0, sizeLong (f));
        int lastPct = -1;
        for (auto& f : files)
        {
            const auto out = dst.getChildFile (f.getRelativePathFrom (src));
            const auto sz = sizeLong (f);
            if (sizeLong (out) != sz && ! copyLong (f, out))
                return u8 ("Impossible de copier ") + f.getFileName() + u8 (" dans ") + dst.getFullPathName();
            done += juce::jmax ((juce::int64) 0, sz);
            const int pct = total > 0 ? (int) (100 * done / total) : 100;
            if (pct != lastPct && progress) { progress (pct); lastPct = pct; }
        }
        // Variant lit ce fichier pour trouver sa bibliothèque, où qu'elle soit
        const auto dataDir = variantDataDir();
        makeDirLong (dataDir);
        dataDir.getChildFile ("library.txt").replaceWithText (dst.getFullPathName());
        return {};
    }

    // installe les plugins dans chaque dossier ; renvoie un message d'erreur (vide = c'est bon)
    juce::String installPlugins (const juce::StringArray& plugins, const juce::Array<juce::File>& folders)
    {
        for (auto& dst : folders)
        {
            makeDirLong (dst);
            if (! dst.isDirectory()) return u8 ("Impossible de créer ") + dst.getFullPathName();

            // anciens effets aux noms changés : mis de côté (pas supprimés), hors du dossier de plugins
            const auto backup = dst.getParentDirectory().getChildFile ("Variant anciens effets");
            for (auto& n : oldNames())
            {
                if (plugins.contains (n) || plugins.size() <= 1) continue;
                const auto old = dst.getChildFile (n + ".vst3");
                if (old.exists()) { backup.createDirectory(); old.moveFileTo (backup.getNonexistentChildFile (n, ".vst3", false)); }
            }
            auto in = payload();
            if (in == nullptr) return u8 ("Fichier d'installation abîmé : retélécharge Variant.");
            juce::ZipFile zip (in.release(), true);
            for (auto& pn : plugins)   // remplace chaque plugin en entier
            {
                const auto target = dst.getChildFile (pn + ".vst3");
                if (target.exists() && ! target.deleteRecursively())
                {
                    // plugin ouvert dans le DAW : Windows interdit de l'effacer mais autorise à le renommer.
                    for (auto& f : target.findChildFiles (juce::File::findFiles, true))
                        if (! f.deleteFile() && ! f.getFileName().contains (".old"))
                            if (! f.moveFileTo (f.getSiblingFile (f.getFileName() + ".old" + juce::String (juce::Time::currentTimeMillis()))))
                                return u8 ("« ") + pn + u8 (" » est ouvert : ferme ton logiciel de musique puis réessaie.");
                }
            }
            const auto res = zip.uncompressTo (dst, true);
            if (res.failed()) return res.getErrorMessage();
            for (auto& pn : plugins)
                if (! dst.getChildFile (pn + ".vst3").exists()) return u8 ("La copie n'a pas marché (") + pn + ").";
        }
        return {};
    }

    juce::File defaultLibraryTarget (const juce::File& docs)
    {
        const auto src = libraryPackage();
        return docs.getChildFile (src != juce::File() ? src.getFileName() : u8 ("Variant Bibliothèque"));
    }
}

//==============================================================================
class InstallerComponent : public juce::Component, private juce::Thread, private juce::AsyncUpdater
{
public:
    InstallerComponent() : juce::Thread ("install")
    {
        plugins = listPlugins();
        daws = detectDaws();
        folders.add (standardVst3());
        for (auto& f : customVst3Folders()) folders.addIfNotAlreadyThere (f);
        library = defaultLibraryTarget (juce::File::getSpecialLocation (juce::File::userDocumentsDirectory));

        for (auto* b : { &install, &quit, &addFolder, &changeLib }) addAndMakeVisible (b);
        install.setButtonText (u8 ("Installer Variant"));
        install.setColour (juce::TextButton::buttonColourId, accent);
        install.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff10101a));
        install.onClick = [this] { start(); };
        quit.setButtonText ("Fermer");
        quit.onClick = [] { juce::JUCEApplication::quit(); };
        addFolder.setButtonText (u8 ("+ Autre dossier de plugins…"));
        addFolder.onClick = [this]
        {
            chooser = std::make_unique<juce::FileChooser> (u8 ("Dossier où ton logiciel cherche ses plugins VST3"), standardVst3());
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc)
            {
                if (fc.getResult().isDirectory()) { folders.addIfNotAlreadyThere (fc.getResult()); repaint(); }
            });
        };
        changeLib.setButtonText (u8 ("Changer…"));
        changeLib.setVisible (libraryPackage() != juce::File());
        changeLib.onClick = [this]
        {
            chooser = std::make_unique<juce::FileChooser> (u8 ("Où ranger la bibliothèque de sons de Variant ?"), library.getParentDirectory());
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc)
            {
                if (fc.getResult().isDirectory()) { library = fc.getResult().getChildFile (library.getFileName()); repaint(); }
            });
        };
        if (plugins.isEmpty()) { setStatus (u8 ("Fichier d'installation abîmé : retélécharge Variant."), bad); install.setEnabled (false); }
        setSize (660, 470);
    }
    ~InstallerComponent() override { stopThread (10000); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (bg);
        auto r = getLocalBounds().reduced (28, 20);
        g.setColour (text);
        g.setFont (juce::FontOptions (30.0f, juce::Font::bold));
        g.drawText ("VARIANT", r.removeFromTop (38), juce::Justification::centredLeft);
        g.setColour (dim);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText ("@prodbysotam  -  " + juce::String (plugins.size()) + " plugins (Variant + effets)", r.removeFromTop (20), juce::Justification::centredLeft);
        r.removeFromTop (12);

        g.setColour (text);
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText (u8 ("Logiciels de musique trouvés sur ce PC"), r.removeFromTop (22), juce::Justification::centredLeft);
        g.setFont (juce::FontOptions (14.0f));
        if (daws.empty())
        {
            g.setColour (dim);
            g.drawText (u8 ("Aucun trouvé : Variant va dans le dossier VST3 standard, lu par tous les logiciels."), r.removeFromTop (20), juce::Justification::centredLeft);
        }
        for (auto& d : daws)
        {
            auto row = r.removeFromTop (20);
            g.setColour (d.ok ? good : warn);
            g.fillEllipse (row.removeFromLeft (20).toFloat().withSizeKeepingCentre (9.0f, 9.0f));   // pastille verte = compatible, orange = non
            g.setColour (text);
            g.drawText (d.name, row.removeFromLeft (130), juce::Justification::centredLeft);
            g.setColour (dim);
            g.drawText (d.note, row, juce::Justification::centredLeft, true);
        }
        r.removeFromTop (10);
        g.setColour (text);
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText (u8 ("Les plugins vont dans"), r.removeFromTop (22), juce::Justification::centredLeft);
        g.setFont (juce::FontOptions (14.0f));
        g.setColour (dim);
        for (auto& f : folders) g.drawText (f.getFullPathName(), r.removeFromTop (19), juce::Justification::centredLeft, true);
        addFolderY = r.getY() + 2;
        r.removeFromTop (34);
        if (libraryPackage() != juce::File())
        {
            g.setColour (text);
            g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
            g.drawText (u8 ("Bibliothèque de sons (3 Go)"), r.removeFromTop (22), juce::Justification::centredLeft);
            g.setFont (juce::FontOptions (14.0f));
            g.setColour (dim);
            libY = r.getY();
            g.drawText (library.getFullPathName(), r.removeFromTop (20).withTrimmedRight (100), juce::Justification::centredLeft, true);
            r.removeFromTop (6);
        }
        g.setColour (dim);
        g.setFont (juce::FontOptions (13.0f));
        g.drawFittedText (u8 ("Ferme ton logiciel de musique avant de cliquer. Ensuite, refais un scan des plugins (FL : Options > Manage plugins > Find more plugins ; "
                              "Ableton : Préférences > Plug-ins > Rescan)."), r.removeFromTop (36), juce::Justification::topLeft, 2);
        juce::String st; juce::Colour sc;
        { const juce::ScopedLock sl (statusLock); st = status; sc = statusCol; }
        g.setColour (sc);
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawFittedText (st, r.removeFromTop (44), juce::Justification::topLeft, 2);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (28, 20).removeFromBottom (40);
        quit.setBounds (r.removeFromRight (110));
        r.removeFromRight (12);
        install.setBounds (r.removeFromRight (220));
        addFolder.setBounds (28, juce::jmax (0, addFolderY), 230, 26);
        changeLib.setBounds (getWidth() - 28 - 90, juce::jmax (0, libY - 3), 90, 24);
    }
    void paintOverChildren (juce::Graphics&) override
    {
        // les boutons suivent la mise en page (la liste des logiciels change de hauteur)
        if (addFolder.getY() != addFolderY || changeLib.getY() != libY - 3) resized();
    }

private:
    void start()
    {
        install.setEnabled (false); addFolder.setEnabled (false); changeLib.setEnabled (false);
        setStatus (u8 ("Installation…"), dim);
        startThread();
    }
    void setStatus (const juce::String& s, juce::Colour c)
    {
        { const juce::ScopedLock sl (statusLock); status = s; statusCol = c; }
        triggerAsyncUpdate();
    }
    void run() override
    {
        auto err = installPlugins (plugins, folders);
        if (err.isEmpty())
            err = copyLibrary (library, [this] (int pct) { setStatus (u8 ("Copie des sons… ") + juce::String (pct) + " %", dim); });
        ok = err.isEmpty();
        setStatus (ok ? u8 ("C'est installé ! Ouvre ton logiciel de musique et fais un scan des plugins : Variant est dans les instruments.")
                      : u8 ("Problème : ") + err, ok ? good : bad);
    }
    void handleAsyncUpdate() override
    {
        if (! isThreadRunning())
        {
            install.setEnabled (! ok);
            install.setButtonText (ok ? u8 ("Installé") : u8 ("Réessayer"));
            addFolder.setEnabled (! ok); changeLib.setEnabled (! ok);
        }
        repaint();
    }

    juce::TextButton install, quit, addFolder, changeLib;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::StringArray plugins;
    std::vector<Daw> daws;
    juce::Array<juce::File> folders;
    juce::File library;
    juce::CriticalSection statusLock;
    juce::String status;
    juce::Colour statusCol { dim };
    int addFolderY = 0, libY = 0;
    bool ok = false;
};

class InstallerApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Installer Variant"; }
    const juce::String getApplicationVersion() override { return "1.0"; }
    void initialise (const juce::String& cmd) override
    {
        // tests sans fenêtre : --silent [--vst3=dossier] [--lib=dossier] [--appdata=dossier] [--list]
        juce::File vst3, lib;
        for (auto& a : juce::StringArray::fromTokens (cmd, true))
        {
            const auto v = a.fromFirstOccurrenceOf ("=", false, false).unquoted();
            if (a.startsWith ("--vst3=")) vst3 = juce::File (v);
            if (a.startsWith ("--lib=")) lib = juce::File (v);
            if (a.startsWith ("--appdata=")) appDataOverride() = juce::File (v);
        }
        if (cmd.contains ("--list"))   // ce que l'installateur a trouvé (logiciels, dossiers)
        {
            juce::String out;
            for (auto& d : detectDaws()) out << (d.ok ? "OK  " : "NON ") << d.name << " : " << d.note << "\n";
            for (auto& f : customVst3Folders()) out << "dossier perso : " << f.getFullPathName() << "\n";
            out << "bibliotheque livree : " << libraryPackage().getFullPathName() << "\n";
            juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("variant_install_list.txt").replaceWithText (out);
            quit();
            return;
        }
        if (cmd.contains ("--snapshot="))   // capture de la fenêtre (vérification de la mise en page)
        {
            InstallerComponent c;
            c.setBounds (0, 0, c.getWidth(), c.getHeight());
            c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);   // 1er dessin : les boutons se placent
            auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
            const juce::File out (cmd.fromFirstOccurrenceOf ("--snapshot=", false, false).upToFirstOccurrenceOf (" --", false, false).unquoted());
            out.deleteFile();
            juce::FileOutputStream os (out);
            juce::PNGImageFormat().writeImageToStream (img, os);
            quit();
            return;
        }
        if (cmd.contains ("--silent"))
        {
            juce::Array<juce::File> folders;
            folders.add (vst3 != juce::File() ? vst3 : standardVst3());
            auto err = installPlugins (listPlugins(), folders);
            if (err.isEmpty()) err = copyLibrary (lib != juce::File() ? lib : defaultLibraryTarget (juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)), {});
            if (err.isNotEmpty()) juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("variant_install_error.txt").replaceWithText (err);
            setApplicationReturnValue (err.isEmpty() ? 0 : 1);
            quit();
            return;
        }
        window = std::make_unique<Window>();
    }
    void shutdown() override { window.reset(); }

    struct Window : juce::DocumentWindow
    {
        Window() : juce::DocumentWindow ("Installer Variant", bg, juce::DocumentWindow::closeButton)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new InstallerComponent(), true);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }
        void closeButtonPressed() override { juce::JUCEApplication::quit(); }
    };
    std::unique_ptr<Window> window;
};

START_JUCE_APPLICATION (InstallerApp)
