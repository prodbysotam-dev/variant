#pragma once

// Statistiques d'utilisation de Variant (combien d'installations, combien de gens l'utilisent en ce moment, par @ Instagram).
//
// - Au premier lancement, la personne tape son @ Instagram (écran « Bienvenue »).
// - Un petit fil en arrière-plan envoie au serveur : un numéro d'installation tiré au hasard, ce @ et la version.
//   À l'ouverture, puis toutes les 5 minutes tant que Variant est ouvert. Si internet ou le serveur ne répond pas,
//   il ne se passe rien : Variant marche exactement pareil (aucun blocage, jamais).
// - Le créateur voit les chiffres dans Réglages > Statistiques de Variant, avec sa clé.

#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "Library.h"
#include "I18n.h"

namespace telemetry
{
    // variables de test (outils de dev). Version publique (VARIANT_PUBLIC) : aucune n est lue,
    // personne ne peut contourner le blocage ni ouvrir les outils du créateur avec elles.
    inline juce::String devEnv (const char* name, const juce::String& def = {})
    {
       #if VARIANT_PUBLIC
        juce::ignoreUnused (name); return def;
       #else
        return juce::SystemStats::getEnvironmentVariable (name, def);
       #endif
    }

    // outils du créateur (statistiques, bannir, messages Kick) : absents de la version publique
    constexpr bool ownerTools()
    {
       #if VARIANT_PUBLIC
        return false;
       #else
        return true;
       #endif
    }

    // adresse du serveur (Cloudflare Worker), à remplir une fois le serveur créé. Vide = rien n'est envoyé.
    inline const char* defaultServer() { return "https://variant-stats.forestleo07.workers.dev"; }

    inline juce::String serverUrl()
    {
        auto u = devEnv ("VARIANT_STATS_URL", {});   // pour les tests
        if (u.isEmpty()) u = defaultServer();
        return u.trim().trimCharactersAtEnd ("/");
    }

    // @ Instagram : sans le @, en minuscules, lettres / chiffres / point / tiret bas, 30 caractères max (règles d'Instagram)
    inline juce::String cleanHandle (juce::String s)
    {
        s = s.trim().toLowerCase();
        while (s.startsWithChar ('@')) s = s.substring (1);
        juce::String out;
        for (auto c : s) if (juce::CharacterFunctions::isLetterOrDigit (c) && c < 128) out << c; else if (c == '.' || c == '_') out << c;
        return out.substring (0, 30);
    }

    // Le @ et le numéro d'installation ont chacun leur petit fichier (dans %APPDATA%\Variant) :
    // le fichier de réglages commun est réécrit par chaque Variant ouvert (FL + version autonome…),
    // ces deux valeurs ne doivent jamais se perdre (sinon un même PC compterait pour plusieurs installations).
    inline juce::File dataFile (const char* name)
    {
        return library::appDataDir().getChildFile ("Variant").getChildFile (name);
    }

    inline juce::String handle()
    {
        const auto t = devEnv ("VARIANT_TEST_HANDLE", {});   // pour les tests
        if (t.isNotEmpty()) return t;
        return dataFile ("instagram.txt").loadFileAsString().trim();
    }

    inline void storeHandle (const juce::String& h)
    {
        auto f = dataFile ("instagram.txt");
        f.getParentDirectory().createDirectory();
        f.replaceWithText (h);
    }

    inline juce::String installId()
    {
        auto f = dataFile ("install.id");
        auto id = f.loadFileAsString().trim();
        if (id.length() < 16)
        {
            id = juce::Uuid().toDashedString();
            f.getParentDirectory().createDirectory();
            f.replaceWithText (id);
        }
        return id;
    }

    inline juce::String versionString()
    {
       #ifdef JucePlugin_VersionString
        return JucePlugin_VersionString;
       #else
        return "dev";
       #endif
    }

    // requête HTTP annulable (pour ne jamais retenir FL à la fermeture)
    struct Request
    {
        juce::CriticalSection lock;
        juce::WebInputStream* current = nullptr;
        bool cancelled = false;

        // renvoie le corps de la réponse si le code HTTP est 200, sinon une chaîne vide
        juce::String run (const juce::URL& url, const juce::String& headers, int& status, int timeoutMs = 6000)
        {
            status = 0;
            juce::WebInputStream ws (url, url.getPostData().isNotEmpty());
            ws.withExtraHeaders (headers).withConnectionTimeout (timeoutMs);
            {
                const juce::ScopedLock sl (lock);
                if (cancelled) return {};
                current = &ws;
            }
            juce::String body;
            if (ws.connect (nullptr))
            {
                status = ws.getStatusCode();
                body = ws.readEntireStreamAsString();
            }
            const juce::ScopedLock sl (lock);
            current = nullptr;
            return status == 200 ? body : juce::String();
        }

        void cancel()
        {
            const juce::ScopedLock sl (lock);
            cancelled = true;
            if (current != nullptr) current->cancel();
        }
    };

    //==========================================================================
    // Accès : Variant ne joue du son que si un @ a été entré, et que ce @ n'a pas été désactivé par le créateur.
    // La désactivation est gardée sur le PC (elle tient même sans internet) et levée dès que le serveur dit « réactivé ».
    // Le PC du créateur (celui qui a la clé des statistiques) n'est jamais désactivé.
    inline std::atomic<bool>& hasHandleFlag() { static std::atomic<bool> f { false }; return f; }
    inline std::atomic<bool>& blockedFlag()   { static std::atomic<bool> f { false }; return f; }
    inline std::atomic<bool>& ownerPcFlag()   { static std::atomic<bool> f { false }; return f; }

    inline bool isBlocked() { return blockedFlag().load (std::memory_order_relaxed) && ! ownerPcFlag().load (std::memory_order_relaxed); }
    inline bool canPlay()   { return hasHandleFlag().load (std::memory_order_relaxed) && ! isBlocked(); }

    inline void setBlocked (bool b)
    {
        if (blockedFlag().exchange (b) == b) return;
        auto f = dataFile ("access.dat");
        if (b) { f.getParentDirectory().createDirectory(); f.replaceWithText ("1"); }
        else f.deleteFile();
    }

    // un @ = un seul PC : si le serveur dit que ce @ appartient à un autre PC, on l'efface ici
    // et l'écran « Bienvenue » revient en expliquant pourquoi (l'utilisateur tape son vrai @)
    inline juce::CriticalSection& noticeLock() { static juce::CriticalSection l; return l; }
    inline juce::String& takenNotice() { static juce::String s; return s; }

    inline void dropTakenHandle (const juce::String& h)
    {
        { const juce::ScopedLock sl (noticeLock()); takenNotice() = h; }
        dataFile ("instagram.txt").deleteFile();
        hasHandleFlag() = false;
    }

    inline bool takenPending()
    {
        const juce::ScopedLock sl (noticeLock());
        return takenNotice().isNotEmpty();
    }

    inline juce::String consumeTakenNotice()
    {
        const juce::ScopedLock sl (noticeLock());
        auto s = takenNotice();
        takenNotice().clear();
        return s;
    }

    // empreinte du PC (matériel) : la même même si Variant est réinstallé ou si la personne change de @
    // (sert au bannissement par PC). Aucune info lisible : juste un code.
    inline juce::String pcId()
    {
        static const juce::String id = []
        {
            juce::String raw;
            for (auto& s : juce::SystemStats::getMachineIdentifiers (juce::SystemStats::MachineIdFlags::uniqueId | juce::SystemStats::MachineIdFlags::fileSystemId)) raw << s << "|";
            if (raw.isEmpty()) raw = juce::SystemStats::getComputerName() + juce::SystemStats::getCpuModel();
            const auto a = (juce::uint64) ("variant-pc-1|" + raw).hashCode64(), b = (juce::uint64) (raw + "|variant-pc-2").hashCode64();
            return juce::String::toHexString ((juce::int64) a).paddedLeft ('0', 16) + juce::String::toHexString ((juce::int64) b).paddedLeft ('0', 16);
        }();
        return id;
    }

    // messages du créateur (envoyés depuis l'écran Statistiques) : Kick les dit à la prochaine ouverture
    inline juce::CriticalSection& inboxLock() { static juce::CriticalSection l; return l; }
    inline juce::StringArray& inbox() { static juce::StringArray a; return a; }
    inline juce::String popMessage()
    {
        const juce::ScopedLock sl (inboxLock());
        if (inbox().isEmpty()) return {};
        auto m = inbox()[0];
        inbox().remove (0);
        return m;
    }
    inline juce::int64 lastMessageId() { return dataFile ("msg.last").loadFileAsString().getLargeIntValue(); }
    // range les messages du créateur reçus du serveur (sans doublon) : Kick les dit tout de suite
    inline void takeMessages (const juce::var& v)
    {
        static juce::CriticalSection idLock;
        const juce::ScopedLock il (idLock);
        if (auto* msgs = v["messages"].getArray())
        {
            juce::int64 last = lastMessageId();
            for (auto& m : *msgs)
            {
                const auto mid = (juce::int64) m["id"];
                if (mid <= last) continue;
                last = mid;
                const juce::ScopedLock sl (inboxLock());
                inbox().add (m["text"].toString().substring (0, 500));
            }
            if (last > lastMessageId()) { auto f = dataFile ("msg.last"); f.getParentDirectory().createDirectory(); f.replaceWithText (juce::String (last)); }
        }
    }

    inline void loadAccess()
    {
        hasHandleFlag() = cleanHandle (handle()).isNotEmpty();
        blockedFlag() = dataFile ("access.dat").existsAsFile();
        ownerPcFlag() = ownerTools() && (dataFile ("stats.key").existsAsFile()
                                          || devEnv ("VARIANT_TEST_OWNER_KEY", {}).isNotEmpty());
    }

    //==========================================================================
    // le fil qui envoie « Variant est ouvert » (un seul pour tous les Variant ouverts dans FL)
    class Pinger : private juce::Thread
    {
    public:
        Pinger (juce::String serverUrl, juce::String installId, juce::String handle)
            : juce::Thread ("Variant stats"), url (std::move (serverUrl)), id (std::move (installId)), who (std::move (handle))
        {
            startThread (juce::Thread::Priority::low);
        }

        ~Pinger() override
        {
            signalThreadShouldExit();
            req.cancel();
            notify();
            stopThread (4000);
        }

        void setHandle (const juce::String& h)
        {
            { const juce::ScopedLock sl (lock); who = h; }
            notify();   // envoi tout de suite
        }

    private:
        void run() override
        {
            wait (3000);   // on laisse FL finir de charger
            bool openSent = false;
            while (! threadShouldExit())
            {
                juce::String h;
                { const juce::ScopedLock sl (lock); h = who; }
                bool ok = false;
                if (url.isNotEmpty() && h.isNotEmpty())
                {
                    auto* o = new juce::DynamicObject();
                    o->setProperty ("id", id);
                    o->setProperty ("handle", h);
                    o->setProperty ("version", versionString());
                    o->setProperty ("open", ! openSent);
                    o->setProperty ("pc", pcId());
                    o->setProperty ("lastMsg", lastMessageId());
                    const auto body = juce::JSON::toString (juce::var (o), true);
                    int status = 0;
                    const auto resp = req.run (juce::URL (url + "/v1/ping").withPOSTData (body), "Content-Type: application/json", status);
                    ok = status == 200;
                    if (ok)
                    {
                        openSent = true;
                        // seule une vraie réponse du serveur change l'accès (une panne ne désactive jamais personne)
                        const auto v = juce::JSON::parse (resp);
                        if (v.isObject() && v.hasProperty ("blocked")) setBlocked ((bool) v["blocked"]);
                        takeMessages (v);   // messages du créateur pour Kick
                        if (v.isObject() && (bool) v["taken"] && devEnv ("VARIANT_TEST_HANDLE", {}).isEmpty())
                        {
                            // ce @ est déjà à un autre PC : on arrête d'envoyer avec lui, l'écran « Bienvenue » revient
                            { const juce::ScopedLock sl (lock); who.clear(); }
                            dropTakenHandle (h);
                        }
                    }
                }
                if (threadShouldExit()) break;
                // raté (pas d'internet…) : nouvel essai dans une minute. Désactivé : on revérifie chaque minute (réactivation rapide)
                wait (ok && ! blockedFlag().load() ? 5 * 60 * 1000 : 60 * 1000);
            }
        }

        const juce::String url, id;
        juce::CriticalSection lock;
        juce::String who;
        Request req;
    };

    inline std::unique_ptr<Pinger>& pingerHolder() { static std::unique_ptr<Pinger> p; return p; }

    //==========================================================================
    // le fil « en direct » : une requête qui attend sur le serveur (jusqu'à 25 s) et revient dès que le créateur
    // envoie un message (ou bannit ce PC) : Kick le dit en temps réel, pas au prochain envoi des stats
    class Live : private juce::Thread
    {
    public:
        Live (juce::String serverUrl, juce::String installId)
            : juce::Thread ("Variant direct"), url (std::move (serverUrl)), id (std::move (installId))
        {
            startThread (juce::Thread::Priority::low);
        }
        ~Live() override
        {
            signalThreadShouldExit();
            req.cancel();
            notify();
            stopThread (4000);
        }

    private:
        void run() override
        {
            wait (4000);
            while (! threadShouldExit())
            {
                const auto h = cleanHandle (handle());
                if (url.isEmpty() || h.isEmpty()) { wait (10000); continue; }
                auto* o = new juce::DynamicObject();
                o->setProperty ("id", id);
                o->setProperty ("handle", h);
                o->setProperty ("pc", pcId());
                o->setProperty ("lastMsg", lastMessageId());
                o->setProperty ("blocked", blockedFlag().load());
                int status = 0;
                const auto resp = req.run (juce::URL (url + "/v1/live").withPOSTData (juce::JSON::toString (juce::var (o), true)),
                                           "Content-Type: application/json", status, 45000);
                if (threadShouldExit()) break;
                if (status == 200)
                {
                    const auto v = juce::JSON::parse (resp);
                    takeMessages (v);
                    if (v.isObject() && v.hasProperty ("blocked")) setBlocked ((bool) v["blocked"]);
                    wait (500);
                }
                else wait (status == 404 ? 5 * 60 * 1000 : 15000);   // 404 : serveur pas encore mis à jour
            }
        }

        const juce::String url, id;
        Request req;
    };

    inline std::unique_ptr<Live>& liveHolder() { static std::unique_ptr<Live> p; return p; }

    // appelé quand le premier Variant s'ouvre / quand le dernier se ferme
    inline void start()
    {
        loadAccess();
        auto& p = pingerHolder();
        if (p == nullptr && serverUrl().isNotEmpty()) p = std::make_unique<Pinger> (serverUrl(), installId(), cleanHandle (handle()));
        auto& lv = liveHolder();
        if (lv == nullptr && serverUrl().isNotEmpty()) lv = std::make_unique<Live> (serverUrl(), installId());
    }
    inline void stop() { pingerHolder().reset(); liveHolder().reset(); }

    inline void setHandle (const juce::String& h)
    {
        storeHandle (h);
        hasHandleFlag() = true;
        if (auto& p = pingerHolder()) p->setHandle (h);
        else start();
    }

    //==========================================================================
    // Écran affiché quand le créateur a désactivé Variant pour ce @
    class BlockedScreen : public juce::Component
    {
    public:
        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xf5101014));
            auto c = getLocalBounds().withSizeKeepingCentre (juce::jmin (560, getWidth() - 40), 240);
            g.setColour (juce::Colour (0xff1c1c22));
            g.fillRoundedRectangle (c.toFloat(), 14.0f);
            g.setColour (juce::Colour (0xffff8a8a).withAlpha (0.7f));
            g.drawRoundedRectangle (c.toFloat().reduced (0.5f), 14.0f, 1.2f);
            auto r = c.reduced (28, 24);
            g.setColour (juce::Colours::white);
            g.setFont (juce::Font (juce::FontOptions (22.0f, juce::Font::bold)));
            g.drawText (fr ("Variant est désactivé"), r.removeFromTop (36), juce::Justification::centred);
            r.removeFromTop (10);
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.setFont (juce::Font (juce::FontOptions (15.0f)));
            g.drawFittedText (fr ("Variant a été désactivé pour @") + cleanHandle (handle())
                                + fr (" : les étapes pour l'obtenir n'ont pas été faites.\n\n"
                                                          "Fais les étapes du post Instagram, puis écris à @prodbysotam : "
                                                          "Variant se réactivera tout seul (internet nécessaire)."),
                              r, juce::Justification::centredTop, 6);
        }
    };

    //==========================================================================
    // Écran du premier lancement : la personne entre son @ Instagram
    class HandleGate : public juce::Component
    {
    public:
        std::function<void()> onDone;

        HandleGate()
        {
            field.setTextToShowWhenEmpty ("@ton_pseudo", juce::Colours::grey);
            field.setFont (juce::Font (juce::FontOptions (18.0f)));
            field.setJustification (juce::Justification::centred);
            field.setInputRestrictions (31);
            field.onReturnKey = [this] { validate(); };
            field.onTextChange = [this] { error.clear(); repaint(); };
            ok.onClick = [this] { validate(); };
            addAndMakeVisible (field);
            addAndMakeVisible (ok);
        }

        ~HandleGate() override { checker.reset(); }

        static bool needed() { return cleanHandle (handle()).isEmpty(); }

        void testSubmit (const juce::String& h) { field.setText (h, false); validate(); }   // outil de test

        void visibilityChanged() override
        {
            if (! isVisible()) return;
            // revenu parce que le @ tapé appartient déjà à un autre PC : on explique
            const auto taken = consumeTakenNotice();
            if (taken.isNotEmpty()) error = takenText (taken);
            juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<HandleGate> (this)] { if (sp != nullptr && sp->isShowing()) sp->field.grabKeyboardFocus(); });
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xf0101014));
            auto c = card();
            g.setColour (juce::Colour (0xff1c1c22));
            g.fillRoundedRectangle (c.toFloat(), 14.0f);
            g.setColour (juce::Colour (0xff9d8df1).withAlpha (0.6f));
            g.drawRoundedRectangle (c.toFloat().reduced (0.5f), 14.0f, 1.2f);
            auto r = c.reduced (28, 22);
            g.setColour (juce::Colours::white);
            g.setFont (juce::Font (juce::FontOptions (22.0f, juce::Font::bold)));
            g.drawText (fr ("Bienvenue sur Variant"), r.removeFromTop (34), juce::Justification::centred);
            g.setColour (juce::Colours::white.withAlpha (0.75f));
            g.setFont (juce::Font (juce::FontOptions (15.0f)));
            g.drawText (fr ("Entre ton @ Instagram pour commencer."), r.removeFromTop (26), juce::Justification::centred);
            if (error.isNotEmpty())
            {
                g.setColour (checking ? juce::Colours::white.withAlpha (0.7f) : juce::Colour (0xffff8a8a));
                g.setFont (juce::Font (juce::FontOptions (13.0f)));
                g.drawFittedText (error, errArea, juce::Justification::centred, 2, 1.0f);
            }
            g.setColour (juce::Colours::white.withAlpha (0.45f));
            g.setFont (juce::Font (juce::FontOptions (12.0f)));
            g.drawFittedText (fr ("Un @ ne peut être utilisé que sur un seul PC. Variant envoie seulement ton @, sa version et "
                                                      "un numéro d'installation tiré au hasard, pour compter combien de personnes l'utilisent. Rien d'autre."),
                              noteArea, juce::Justification::centred, 3);
        }

        void resized() override
        {
            auto r = card().reduced (28, 22);
            r.removeFromTop (34 + 26 + 14);
            field.setBounds (r.removeFromTop (40).withSizeKeepingCentre (300, 40));
            r.removeFromTop (4);
            errArea = r.removeFromTop (36);
            ok.setBounds (r.removeFromTop (36).withSizeKeepingCentre (180, 36));
            r.removeFromTop (12);
            noteArea = r;
        }

    private:
        // vérification du @ auprès du serveur (en arrière-plan, annulable)
        struct ClaimCheck : juce::Thread
        {
            juce::Component::SafePointer<HandleGate> owner;
            juce::String url, id, handle;
            Request req;
            ClaimCheck (HandleGate* g, juce::String u, juce::String i, juce::String h)
                : juce::Thread ("Variant @"), owner (g), url (std::move (u)), id (std::move (i)), handle (std::move (h)) { startThread(); }
            ~ClaimCheck() override { signalThreadShouldExit(); req.cancel(); stopThread (4000); }
            void run() override
            {
                auto* o = new juce::DynamicObject();
                o->setProperty ("id", id);
                o->setProperty ("handle", handle);
                o->setProperty ("pc", pcId());
                int status = 0;
                const auto body = req.run (juce::URL (url + "/v1/claim").withPOSTData (juce::JSON::toString (juce::var (o), true)),
                                           "Content-Type: application/json", status);
                if (threadShouldExit()) return;
                const auto v = juce::JSON::parse (body);
                // 1 = libre (ou déjà à ce PC), 0 = pris par un autre PC, -1 = pas de réponse (pas d'internet…)
                // -2 = ce PC est banni par le créateur
                const int result = (status == 200 && v.isObject()) ? ((bool) v["banned"] ? -2 : ((bool) v["taken"] ? 0 : 1)) : -1;
                const auto h = handle;
                juce::MessageManager::callAsync ([g = owner, h, result] { if (g != nullptr) g->checked (h, result); });
            }
        };

        juce::Rectangle<int> card() const { return getLocalBounds().withSizeKeepingCentre (juce::jmin (520, getWidth() - 40), 330); }

        static juce::String takenText (const juce::String& h)
        {
            return fr ("@") + h + fr (" est déjà utilisé sur un autre PC. "
                                                                               "Si c'est bien ton compte, écris à @prodbysotam sur Instagram.");
        }

        void validate()
        {
            if (checking) return;
            const auto h = cleanHandle (field.getText());
            if (h.isEmpty()) { error = fr ("Écris ton @ Instagram (lettres, chiffres, point ou _)."); repaint(); return; }
            if (serverUrl().isEmpty()) { accept (h); return; }
            checking = true;
            ok.setEnabled (false);
            error = fr ("Vérification du @…");
            repaint();
            checker = std::make_unique<ClaimCheck> (this, serverUrl(), installId(), h);
        }

        void checked (const juce::String& h, int result)
        {
            checking = false;
            ok.setEnabled (true);
            if (result == 0) { error = takenText (h); repaint(); return; }
            if (result == -2) { setBlocked (true); accept (h); return; }   // PC banni : l'écran « désactivé » prend le relais
            // libre — ou serveur injoignable : on laisse jouer, le serveur vérifiera dès que possible
            accept (h);
        }

        void accept (const juce::String& h)
        {
            error.clear();
            setHandle (h);
            setVisible (false);
            if (onDone) onDone();
        }

        juce::TextEditor field;
        juce::TextButton ok { fr ("C'est parti") };
        juce::String error;
        bool checking = false;
        juce::Rectangle<int> errArea, noteArea;
        std::unique_ptr<ClaimCheck> checker;
    };

    //==========================================================================
    //==========================================================================
    // Réglages > Signaler un bug : la personne écrit son problème, le créateur le lit dans ses Statistiques
    class ReportPanel : public juce::Component
    {
    public:
        juce::String hostName;   // le logiciel qui a ouvert Variant (FL Studio, Ableton…), donné par l'éditeur

        ReportPanel()
        {
            for (auto* c : std::initializer_list<juce::Component*> { &text, &send, &cancel }) addAndMakeVisible (c);
            text.setMultiLine (true, true);
            text.setReturnKeyStartsNewLine (true);
            text.setInputRestrictions (2000);
            text.setFont (juce::Font (juce::FontOptions (14.0f)));
            send.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff9d8df1).withAlpha (0.7f));
            send.onClick = [this] { submit(); };
            cancel.onClick = [this] { setVisible (false); };
        }
        ~ReportPanel() override { sender.reset(); }

        void open()
        {
            send.setButtonText (fr ("Envoyer"));
            cancel.setButtonText (fr ("Annuler"));
            text.setTextToShowWhenEmpty (fr ("Ce qui ne marche pas, et ce que tu faisais juste avant (ex : j'ai chargé un preset puis FL a planté)"), juce::Colours::grey);
            status.clear();
            sent = false;
            text.setEnabled (true); send.setEnabled (true);
            setVisible (true);
            toFront (true);
            text.grabKeyboardFocus();
            resized();
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xd8101014));
            const auto c = card();
            g.setColour (juce::Colour (0xff1c1c22));
            g.fillRoundedRectangle (c.toFloat(), 14.0f);
            g.setColour (juce::Colour (0xff9d8df1).withAlpha (0.6f));
            g.drawRoundedRectangle (c.toFloat().reduced (0.5f), 14.0f, 1.2f);
            auto r = c.reduced (22, 16);
            g.setColour (juce::Colours::white);
            g.setFont (juce::Font (juce::FontOptions (17.0f, juce::Font::bold)));
            g.drawText (fr ("Signaler un bug"), r.removeFromTop (28), juce::Justification::centredLeft);
            g.setColour (juce::Colours::white.withAlpha (0.65f));
            g.setFont (juce::Font (juce::FontOptions (13.0f)));
            g.drawFittedText (fr ("Le créateur reçoit ton message avec ton @, ta version de Variant, ton système et ton logiciel de musique."),
                              r.removeFromTop (36), juce::Justification::topLeft, 2);
            g.setColour (sent ? juce::Colour (0xff7ee0a8) : juce::Colours::white.withAlpha (0.8f));
            g.drawFittedText (status, statusArea, juce::Justification::centredLeft, 2);
        }

        void resized() override
        {
            auto r = card().reduced (22, 16);
            r.removeFromTop (28 + 36 + 6);
            auto bottom = r.removeFromBottom (34);
            cancel.setBounds (bottom.removeFromRight (120));
            bottom.removeFromRight (8);
            send.setBounds (bottom.removeFromRight (140));
            bottom.removeFromRight (10);
            statusArea = bottom;
            r.removeFromBottom (10);
            text.setBounds (r);
        }

    private:
        struct Sender : juce::Thread
        {
            juce::Component::SafePointer<ReportPanel> owner;
            juce::String url, body;
            Request req;
            Sender (ReportPanel* p, juce::String u, juce::String b) : juce::Thread ("Variant bug"), owner (p), url (std::move (u)), body (std::move (b)) { startThread(); }
            ~Sender() override { signalThreadShouldExit(); req.cancel(); stopThread (4000); }
            void run() override
            {
                int status = 0;
                req.run (juce::URL (url + "/v1/report").withPOSTData (body), "Content-Type: application/json", status, 10000);
                if (threadShouldExit()) return;
                juce::MessageManager::callAsync ([o = owner, status] { if (o != nullptr) o->done (status); });
            }
        };

        void submit()
        {
            const auto t = text.getText().trim();
            if (t.length() < 3) { status = fr ("Écris d'abord ce qui ne marche pas."); repaint(); return; }
            if (serverUrl().isEmpty()) { status = fr ("Pas pu envoyer : vérifie ta connexion internet et réessaie."); repaint(); return; }
            auto* o = new juce::DynamicObject();
            o->setProperty ("id", installId());
            o->setProperty ("handle", handle());
            o->setProperty ("version", versionString());
            o->setProperty ("os", juce::SystemStats::getOperatingSystemName());
            o->setProperty ("host", hostName);
            o->setProperty ("lang", i18n::languageNames()[i18n::current().load()]);
            o->setProperty ("text", t);
            status = fr ("Envoi…");
            send.setEnabled (false);
            repaint();
            sender = std::make_unique<Sender> (this, serverUrl(), juce::JSON::toString (juce::var (o), true));
        }

        void done (int httpStatus)
        {
            send.setEnabled (true);
            if (httpStatus == 200)
            {
                sent = true;
                status = fr ("Merci ! Le créateur a bien reçu ton message.");
                text.clear();
                juce::Component::SafePointer<ReportPanel> self (this);
                juce::Timer::callAfterDelay (2500, [self] { if (self != nullptr && self->sent) self->setVisible (false); });
            }
            else if (httpStatus == 429) status = fr ("Tu as déjà envoyé beaucoup de messages aujourd'hui. Réessaie demain.");
            else status = fr ("Pas pu envoyer : vérifie ta connexion internet et réessaie.");
            repaint();
        }

        juce::Rectangle<int> card() const
        {
            const int w = juce::jmin (620, getWidth() - 40), h = juce::jmin (380, getHeight() - 40);
            return getLocalBounds().withSizeKeepingCentre (w, h);
        }

        juce::TextEditor text;
        juce::TextButton send, cancel;
        juce::String status;
        juce::Rectangle<int> statusArea;
        bool sent = false;
        std::unique_ptr<Sender> sender;
    };

    // Écran du créateur : Réglages > Statistiques de Variant
   #if VARIANT_PUBLIC
    // version publique : l écran du créateur n existe pas (rien à ouvrir, aucun code d administration)
    class StatsPanel : public juce::Component { public: void open() {} };
   #else
    class StatsPanel : public juce::Component, private juce::Timer
    {
    public:
        StatsPanel()
        {
            for (auto* b : { &refresh, &close, &changeKey, &saveKey, &reportsBtn, &doneBtn }) addAndMakeVisible (b);
            addAndMakeVisible (doneField);
            doneField.setInputRestrictions (8, "0123456789");
            doneField.setTextToShowWhenEmpty (juce::String::fromUTF8 ("N°"), juce::Colours::grey);
            doneBtn.setTooltip (juce::String::fromUTF8 ("Tape le numéro d'un signalement puis « Marquer réglé » : il passe en bas de la liste. Reclique pour le rouvrir."));
            reportsBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xffe0a84d).withAlpha (0.55f));
            reportsBtn.onClick = [this] { showReports = ! showReports; render(); resized(); };
            doneBtn.onClick = [this] { doneAction(); };
            addAndMakeVisible (keyField);
            addAndMakeVisible (list);
            keyField.setPasswordCharacter ((juce::juce_wchar) 0x2022);
            keyField.setTextToShowWhenEmpty (juce::String::fromUTF8 ("Clé des statistiques (OWNER_KEY)"), juce::Colours::grey);
            keyField.onReturnKey = [this] { storeKey(); };
            list.setMultiLine (true);
            list.setReadOnly (true);
            list.setCaretVisible (false);
            list.setScrollbarsShown (true);
            list.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain)));
            refresh.onClick = [this] { fetch(); };
            close.onClick = [this] { setVisible (false); };
            changeKey.onClick = [this] { dataFile ("stats.key").deleteFile(); ownerPcFlag() = false; updateMode(); };
            saveKey.onClick = [this] { storeKey(); };

            // désactiver / réactiver Variant pour un @
            for (auto* c : std::initializer_list<juce::Component*> { &blockField, &blockBtn, &unblockBtn, &releaseBtn }) addAndMakeVisible (c);
            blockField.setTextToShowWhenEmpty (juce::String::fromUTF8 ("@ Instagram"), juce::Colours::grey);
            releaseBtn.setTooltip (juce::String::fromUTF8 ("Un @ ne marche que sur un seul PC. La personne a changé de PC ? « Libérer » : "
                                                           "elle pourra retaper son @ sur le nouveau (l'ancien PC ne doit plus utiliser Variant)."));
            releaseBtn.onClick = [this] { releaseAction(); };
            blockField.setInputRestrictions (31);
            blockBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xffb34d4d).withAlpha (0.55f));
            blockBtn.setTooltip (juce::String::fromUTF8 ("Variant arrête de jouer chez cette personne (sur tous ses PC) dès sa prochaine ouverture, ou dans la minute s'il est ouvert."));
            unblockBtn.setTooltip (juce::String::fromUTF8 ("Variant remarche chez cette personne dans la minute."));
            blockBtn.onClick = [this] { blockAction (true); };
            unblockBtn.onClick = [this] { blockAction (false); };

            // bannir les PC (et leurs adresses IP) d'un @ : plus aucun accès à Variant depuis ces ordinateurs, même avec un autre @
            for (auto* c : std::initializer_list<juce::Component*> { &banPcBtn, &banIpBtn, &unbanBtn, &msgTarget, &msgText, &msgBtn }) addAndMakeVisible (c);
            banPcBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xffb34d4d).withAlpha (0.8f));
            banIpBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff8a1f1f).withAlpha (0.9f));
            banPcBtn.setTooltip (juce::String::fromUTF8 ("Bannit tous les ordinateurs qui ont utilisé ce @ : Variant ne marche plus sur ces PC, même s'ils réinstallent ou changent de @."));
            banIpBtn.setTooltip (juce::String::fromUTF8 ("Pareil + bannit les adresses internet (IP) de ces PC. Attention : ça bloque aussi les autres personnes sur la même connexion."));
            unbanBtn.setTooltip (juce::String::fromUTF8 ("Lève le bannissement des PC et des IP de ce @."));
            banPcBtn.onClick = [this] { banAction (true, false); };
            banIpBtn.onClick = [this] { banAction (true, true); };
            unbanBtn.onClick = [this] { banAction (false, false); };
            msgTarget.setTextToShowWhenEmpty (juce::String::fromUTF8 ("@ (vide = tout le monde)"), juce::Colours::grey);
            msgTarget.setInputRestrictions (31);
            msgText.setTextToShowWhenEmpty (juce::String::fromUTF8 ("Ton message : Kick le dira à l'ouverture de Variant"), juce::Colours::grey);
            msgText.setInputRestrictions (300);
            msgText.onReturnKey = [this] { messageAction(); };
            msgBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff9d8df1).withAlpha (0.6f));
            msgBtn.onClick = [this] { messageAction(); };
        }

        ~StatsPanel() override { stopTimer(); fetcher.reset(); action.reset(); }

        void open()
        {
            if (! ownerTools()) return;   // version publique : pas d écran créateur
            setVisible (true);
            toFront (true);
            updateMode();
        }

        void visibilityChanged() override { if (! isVisible()) stopTimer(); }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xf0101014));
            auto c = card();
            g.setColour (juce::Colour (0xff1c1c22));
            g.fillRoundedRectangle (c.toFloat(), 14.0f);
            g.setColour (juce::Colour (0xff9d8df1).withAlpha (0.6f));
            g.drawRoundedRectangle (c.toFloat().reduced (0.5f), 14.0f, 1.2f);
            auto r = c.reduced (22, 16);
            g.setColour (juce::Colours::white);
            g.setFont (juce::Font (juce::FontOptions (17.0f, juce::Font::bold)));
            g.drawText (juce::String::fromUTF8 ("STATISTIQUES DE VARIANT"), r.removeFromTop (28), juce::Justification::centredLeft);
            g.setColour (juce::Colours::white.withAlpha (0.6f));
            g.setFont (juce::Font (juce::FontOptions (13.0f)));
            g.drawText (status, statusArea, juce::Justification::centredLeft, true);

            if (! hasKey()) return;
            // les gros chiffres
            static const char* labels[] = { "Installations", "En ce moment", "Actifs aujourd'hui", "Nouveaux aujourd'hui", "Actifs 7 jours", "@ différents" };
            for (int i = 0; i < 6; ++i)
            {
                auto box = tiles[(size_t) i];
                g.setColour (juce::Colour (0xff26262e));
                g.fillRoundedRectangle (box.toFloat(), 10.0f);
                g.setColour (i == 1 ? juce::Colour (0xff7ee0a8) : juce::Colours::white);
                g.setFont (juce::Font (juce::FontOptions (26.0f, juce::Font::bold)));
                g.drawText (numbers[(size_t) i], box.withTrimmedBottom (22), juce::Justification::centred);
                g.setColour (juce::Colours::white.withAlpha (0.6f));
                g.setFont (juce::Font (juce::FontOptions (12.0f)));
                g.drawText (juce::String::fromUTF8 (labels[i]), box.removeFromBottom (26), juce::Justification::centred);
            }
        }

        void resized() override
        {
            auto r = card().reduced (22, 16);
            auto top = r.removeFromTop (28);
            close.setBounds (top.removeFromRight (80));
            top.removeFromRight (8);
            refresh.setBounds (top.removeFromRight (100));
            top.removeFromRight (8);
            changeKey.setBounds (top.removeFromRight (110));
            top.removeFromRight (8);
            reportsBtn.setBounds (top.removeFromRight (140));
            r.removeFromTop (6);
            statusArea = r.removeFromTop (20);
            r.removeFromTop (8);
            if (! hasKey())
            {
                auto k = r.removeFromTop (36);
                saveKey.setBounds (k.removeFromRight (110));
                k.removeFromRight (8);
                keyField.setBounds (k);
                return;
            }
            auto row = r.removeFromTop (78);
            const int w = (row.getWidth() - 5 * 8) / 6;
            for (int i = 0; i < 6; ++i) { tiles[(size_t) i] = row.removeFromLeft (w); row.removeFromLeft (8); }
            r.removeFromTop (10);
            auto b = r.removeFromTop (32);
            releaseBtn.setBounds (b.removeFromRight (120));
            b.removeFromRight (8);
            unblockBtn.setBounds (b.removeFromRight (120));
            b.removeFromRight (8);
            blockBtn.setBounds (b.removeFromRight (120));
            b.removeFromRight (8);
            blockField.setBounds (b.removeFromLeft (juce::jmin (300, b.getWidth())));
            r.removeFromTop (10);
            {
                auto b2 = r.removeFromTop (32);
                unbanBtn.setBounds (b2.removeFromRight (120)); b2.removeFromRight (8);
                banIpBtn.setBounds (b2.removeFromRight (130)); b2.removeFromRight (8);
                banPcBtn.setBounds (b2.removeFromRight (130));
                r.removeFromTop (8);
                auto m = r.removeFromTop (32);
                msgBtn.setBounds (m.removeFromRight (150)); m.removeFromRight (8);
                msgTarget.setBounds (m.removeFromLeft (190)); m.removeFromLeft (8);
                msgText.setBounds (m);
                r.removeFromTop (10);
            }
            if (showReports)
            {
                auto d = r.removeFromTop (30);
                doneBtn.setBounds (d.removeFromRight (150)); d.removeFromRight (8);
                doneField.setBounds (d.removeFromRight (90));
                r.removeFromTop (8);
            }
            doneBtn.setVisible (showReports && hasKey()); doneField.setVisible (showReports && hasKey());
            list.setBounds (r);
        }

    private:
        // requête du créateur en arrière-plan (lecture des stats, ou désactivation / réactivation d'un @)
        struct Fetcher : juce::Thread
        {
            juce::Component::SafePointer<StatsPanel> owner;
            juce::String url, key, path, postBody;
            bool isAction;
            Request req;
            Fetcher (StatsPanel* p, juce::String u, juce::String k, juce::String pth, juce::String post, bool act)
                : juce::Thread ("Variant stats lecture"), owner (p), url (std::move (u)), key (std::move (k)),
                  path (std::move (pth)), postBody (std::move (post)), isAction (act) { startThread(); }
            ~Fetcher() override { signalThreadShouldExit(); req.cancel(); stopThread (4000); }
            void run() override
            {
                int status = 0;
                juce::URL u (url + path);
                juce::String headers = "X-Owner-Key: " + key;
                if (postBody.isNotEmpty()) { u = u.withPOSTData (postBody); headers << "\r\nContent-Type: application/json"; }
                const auto body = req.run (u, headers, status);
                if (threadShouldExit()) return;
                const bool act = isAction;
                juce::MessageManager::callAsync ([o = owner, body, status, act]
                {
                    if (o == nullptr) return;
                    if (act) o->actionDone (body, status); else o->show (body, status);
                });
            }
        };

        void blockAction (bool block)
        {
            const auto h = cleanHandle (blockField.getText());
            if (h.isEmpty() || serverUrl().isEmpty() || ! hasKey()) return;
            auto* o = new juce::DynamicObject();
            o->setProperty ("handle", h);
            o->setProperty ("blocked", block);
            status = (block ? juce::String::fromUTF8 ("Désactivation de @") : juce::String::fromUTF8 ("Réactivation de @")) + h + juce::String::fromUTF8 ("…");
            repaint();
            action = std::make_unique<Fetcher> (this, serverUrl(), key(), "/v1/block", juce::JSON::toString (juce::var (o), true), true);
        }

        void banAction (bool ban, bool withIp)
        {
            const auto h = cleanHandle (blockField.getText());
            if (h.isEmpty() || serverUrl().isEmpty() || ! hasKey()) { status = juce::String::fromUTF8 ("Tape d'abord le @ dans le champ « @ Instagram »."); repaint(); return; }
            auto* o = new juce::DynamicObject();
            o->setProperty ("handle", h);
            o->setProperty ("banned", ban);
            o->setProperty ("ip", withIp);
            status = (ban ? juce::String::fromUTF8 ("Bannissement des PC de @") : juce::String::fromUTF8 ("Débannissement de @")) + h + juce::String::fromUTF8 ("…");
            repaint();
            action = std::make_unique<Fetcher> (this, serverUrl(), key(), "/v1/ban", juce::JSON::toString (juce::var (o), true), true);
        }

        void messageAction()
        {
            const auto text = msgText.getText().trim();
            if (text.isEmpty() || serverUrl().isEmpty() || ! hasKey()) return;
            auto* o = new juce::DynamicObject();
            o->setProperty ("target", cleanHandle (msgTarget.getText()));
            o->setProperty ("text", text);
            status = juce::String::fromUTF8 ("Envoi du message…");
            repaint();
            action = std::make_unique<Fetcher> (this, serverUrl(), key(), "/v1/message", juce::JSON::toString (juce::var (o), true), true);
        }

        void releaseAction()
        {
            const auto h = cleanHandle (blockField.getText());
            if (h.isEmpty() || serverUrl().isEmpty() || ! hasKey()) return;
            auto* o = new juce::DynamicObject();
            o->setProperty ("handle", h);
            status = juce::String::fromUTF8 ("Libération de @") + h + juce::String::fromUTF8 ("…");
            repaint();
            action = std::make_unique<Fetcher> (this, serverUrl(), key(), "/v1/release", juce::JSON::toString (juce::var (o), true), true);
        }

        void actionDone (const juce::String& body, int httpStatus)
        {
            const auto v = juce::JSON::parse (body);
            if (httpStatus == 403) status = juce::String::fromUTF8 ("Clé refusée : clique « Changer la clé » et retape-la.");
            else if (httpStatus != 200 || ! v.isObject())
                status = juce::String::fromUTF8 ("Pas pu joindre le serveur : rien n'a changé. Réessaie dans un instant.");
            else if (v.hasProperty ("sent"))
            {
                const auto t = v["target"].toString();
                status = juce::String::fromUTF8 ("Message envoyé ") + (t.isEmpty() ? juce::String::fromUTF8 ("à tout le monde") : "à @" + t)
                         + juce::String::fromUTF8 (" : Kick l'affiche en quelques secondes (ou à l'ouverture de Variant).");
                msgText.clear();
                note = status;
            }
            else if (v.hasProperty ("reportDone"))
            {
                status = juce::String::fromUTF8 ("Signalement n°") + v["reportDone"].toString() + juce::String::fromUTF8 (" mis à jour.");
                doneField.clear();
                note = status;
                fetch();
                status = note;
            }
            else if (v.hasProperty ("pcs"))
            {
                const auto h = v["handle"].toString();
                status = (bool) v["banned"]
                    ? "@" + h + juce::String::fromUTF8 (" banni : ") + juce::String ((int) v["pcs"]) + juce::String::fromUTF8 (" PC") + ((int) v["ips"] > 0 ? juce::String::fromUTF8 (" et ") + juce::String ((int) v["ips"]) + " IP" : juce::String())
                      + juce::String::fromUTF8 (" bloqués. Variant s'arrête chez lui dans la minute.")
                    : "@" + h + juce::String::fromUTF8 (" débanni : ses PC peuvent de nouveau utiliser Variant.");
                blockField.clear();
                note = status;
                fetch();
                status = note;
            }
            else
            {
                const auto h = v["handle"].toString();
                status = (bool) v["released"]
                    ? "@" + h + juce::String::fromUTF8 (" libéré : la personne peut le retaper sur son nouveau PC.")
                    : (bool) v["blocked"]
                    ? "@" + h + juce::String::fromUTF8 (" désactivé : Variant s'arrête chez lui dans la minute (ou à sa prochaine ouverture).")
                    : "@" + h + juce::String::fromUTF8 (" réactivé : Variant remarche chez lui dans la minute.");
                blockField.clear();
                note = status;   // gardé à l'écran après la mise à jour de la liste
                fetch();
                status = note;
            }
            repaint();
        }

        juce::Rectangle<int> card() const { return getLocalBounds().reduced (juce::jmax (20, getWidth() / 12), juce::jmax (20, getHeight() / 12)); }
        static juce::String key()
        {
            const auto t = devEnv ("VARIANT_TEST_OWNER_KEY", {});   // pour les tests
            return t.isNotEmpty() ? t : dataFile ("stats.key").loadFileAsString().trim();
        }
        bool hasKey() const { return key().isNotEmpty(); }

        void storeKey()
        {
            const auto k = keyField.getText().trim();
            if (k.isEmpty()) return;
            { auto f = dataFile ("stats.key"); f.getParentDirectory().createDirectory(); f.replaceWithText (k); }
            ownerPcFlag() = true;   // le PC du créateur n'est jamais désactivé
            keyField.clear();
            updateMode();
        }

        void updateMode()
        {
            const bool k = hasKey();
            keyField.setVisible (! k); saveKey.setVisible (! k);
            list.setVisible (k); refresh.setVisible (k); changeKey.setVisible (k); reportsBtn.setVisible (k);
            blockField.setVisible (k); blockBtn.setVisible (k); unblockBtn.setVisible (k); releaseBtn.setVisible (k);
            for (juce::Component* c : { (juce::Component*) &banPcBtn, (juce::Component*) &banIpBtn, (juce::Component*) &unbanBtn, (juce::Component*) &msgTarget, (juce::Component*) &msgText, (juce::Component*) &msgBtn }) c->setVisible (k);
            if (serverUrl().isEmpty()) status = juce::String::fromUTF8 ("Serveur pas encore relié à Variant : rien n'est compté pour l'instant.");
            else if (! k) status = juce::String::fromUTF8 ("Entre ta clé des statistiques (la même que OWNER_KEY dans Cloudflare).");
            resized();
            repaint();
            if (k) { fetch(); startTimer (30000); }   // mis à jour toutes les 30 s tant que l'écran est ouvert
            else stopTimer();
        }

        void timerCallback() override { fetch(); }

        void fetch()
        {
            if (serverUrl().isEmpty() || ! hasKey()) return;
            status = juce::String::fromUTF8 ("Mise à jour…");
            repaint();
            fetcher = std::make_unique<Fetcher> (this, serverUrl(), key(), "/v1/stats", juce::String(), false);
        }

        static juce::String when (juce::int64 ms)
        {
            if (ms <= 0) return "-";
            return juce::Time (ms).formatted ("%d/%m %H:%M");
        }

        void show (const juce::String& body, int httpStatus)
        {
            if (httpStatus == 403) { status = juce::String::fromUTF8 ("Clé refusée : clique « Changer la clé » et retape-la."); repaint(); return; }
            const auto v = juce::JSON::parse (body);
            if (httpStatus != 200 || ! v.isObject())
            {
                status = juce::String::fromUTF8 ("Serveur injoignable pour l'instant (internet ?). Nouvel essai dans 30 s.");
                repaint();
                return;
            }
            const char* keys[] = { "installs", "activeNow", "today", "newToday", "week", "handles" };
            for (int i = 0; i < 6; ++i) numbers[(size_t) i] = juce::String ((juce::int64) v[keys[i]]);
            data = v;
            render();
            status = note.isNotEmpty() ? note
                   : juce::String::fromUTF8 ("Mis à jour à ") + juce::Time::getCurrentTime().formatted ("%H:%M:%S")
                     + juce::String::fromUTF8 ("  ·  « En ce moment » = Variant ouvert dans les 11 dernières minutes");
            note.clear();
            repaint();
        }

        // la liste : les @ (statistiques) ou les bugs signalés
        void render()
        {
            const auto& v = data;
            const int open = (int) v["reportsOpen"];
            reportsBtn.setButtonText (showReports ? juce::String::fromUTF8 ("← Statistiques")
                                                  : juce::String::fromUTF8 ("Signalements") + (open > 0 ? " (" + juce::String (open) + ")" : juce::String()));
            if (! v.isObject()) return;
            if (showReports)
            {
                juce::String t;
                t << juce::String::fromUTF8 ("BUGS SIGNALÉS  ·  ") << open << juce::String::fromUTF8 (" à régler\n");
                t << juce::String::repeatedString ("-", 82) << "\n";
                auto* arr = v["reports"].getArray();
                if (arr == nullptr || arr->isEmpty()) t << juce::String::fromUTF8 ("Aucun signalement pour l'instant.\n");
                else
                    for (auto& row : *arr)
                    {
                        t << "n°" << row["id"].toString() << ((int) row["done"] != 0 ? juce::String::fromUTF8 ("  [RÉGLÉ]") : juce::String())
                          << "  " << when ((juce::int64) row["at"]) << "  @" << row["handle"].toString()
                          << "  v" << row["version"].toString() << "  " << row["lang"].toString() << "\n";
                        t << "   " << row["os"].toString() << "  ·  " << row["host"].toString() << "\n";
                        t << "   " << row["text"].toString().replace ("\n", "\n   ") << "\n\n";
                    }
                list.setText (t, false);
                return;
            }

            juce::String t;
            // colonnes : @ | installations | ouverts maintenant | nombre d'ouvertures | 1re fois | dernière fois
            t << juce::String::fromUTF8 ("@ INSTAGRAM                 INSTALLS  EN DIRECT  OUVERT.  1re FOIS     DERNIÈRE\n");
            t << juce::String::repeatedString ("-", 82) << "\n";
            if (auto* arr = v["byHandle"].getArray())
                for (auto& row : *arr)
                {
                    const int n = (int) row["installs"];
                    auto name = "@" + row["handle"].toString();
                    if (name.length() > 22) name = name.substring (0, 21) + juce::String::fromUTF8 ("…");
                    if ((int) row["blocked"] != 0) name << " [OFF]";   // désactivé par toi
                    else if (n >= 3) name << " (!)";   // beaucoup d'installations pour un seul @ : lien partagé ?
                    t << name.paddedRight (' ', 28)
                      << juce::String (n).paddedLeft (' ', 8)
                      << juce::String ((int) row["active"]).paddedLeft (' ', 11)
                      << juce::String ((int) row["opens"]).paddedLeft (' ', 9)
                      << "  " << when ((juce::int64) row["firstSeen"]).paddedRight (' ', 13)
                      << when ((juce::int64) row["lastSeen"]) << "\n";
                }
            t << "\n" << juce::String::fromUTF8 ("(!) = 3 installations ou plus avec le même @ : le lien a peut-être été partagé.\n"
                                                 "[OFF] = Variant désactivé pour ce @ (tape le @ en haut puis « Réactiver » pour annuler).\n");
            list.setText (t, false);
        }

        void doneAction()
        {
            const auto n = doneField.getText().trim().getIntValue();
            if (n <= 0 || serverUrl().isEmpty() || ! hasKey()) { status = juce::String::fromUTF8 ("Tape d'abord le numéro du signalement (n°…)."); repaint(); return; }
            bool isDone = false;
            if (auto* arr = data["reports"].getArray())
                for (auto& row : *arr) if ((int) row["id"] == n) isDone = (int) row["done"] != 0;
            auto* o = new juce::DynamicObject();
            o->setProperty ("id", n);
            o->setProperty ("done", ! isDone);
            status = juce::String::fromUTF8 ("Mise à jour du signalement…");
            repaint();
            action = std::make_unique<Fetcher> (this, serverUrl(), key(), "/v1/report-done", juce::JSON::toString (juce::var (o), true), true);
        }

        juce::TextButton refresh { "Actualiser" }, close { "Fermer" }, changeKey { juce::String::fromUTF8 ("Changer la clé") }, saveKey { "Valider" };
        juce::TextEditor keyField, list;
        juce::String status;
        juce::Rectangle<int> statusArea;
        std::array<juce::Rectangle<int>, 6> tiles;
        std::array<juce::String, 6> numbers { "-", "-", "-", "-", "-", "-" };
        juce::String note;
        juce::TextEditor blockField;
        juce::TextButton blockBtn { juce::String::fromUTF8 ("Désactiver") }, unblockBtn { juce::String::fromUTF8 ("Réactiver") }, releaseBtn { juce::String::fromUTF8 ("Libérer le @") };
        juce::TextButton banPcBtn { juce::String::fromUTF8 ("Bannir ses PC") }, banIpBtn { juce::String::fromUTF8 ("Bannir PC + IP") }, unbanBtn { juce::String::fromUTF8 ("Débannir") };
        juce::TextEditor msgTarget, msgText;
        juce::TextButton msgBtn { juce::String::fromUTF8 ("Envoyer via Kick") };
        juce::TextButton reportsBtn { juce::String::fromUTF8 ("Signalements") }, doneBtn { juce::String::fromUTF8 ("Marquer réglé") };
        juce::TextEditor doneField;
        bool showReports = false;
        juce::var data;   // dernière réponse du serveur
        std::unique_ptr<Fetcher> fetcher, action;
    };
   #endif
}
