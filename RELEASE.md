# Submit MetaModule — Release Workflow

## Bestandsnaam
De plugin heet altijd: `Submit-v2.1.mmplugin`
- `v2.1` = MetaModule SDK versie (verandert alleen bij SDK upgrade)
- De bestandsnaam verandert NOOIT tenzij de SDK versie omhoog gaat

---

## Release maken

### Stap 1 — plugin.json synchroniseren
Controleer of plugin.json in Submit-MM up-to-date is met de VCV versie:
```bash
cp ~/Submit/plugin.json ~/Submit-MM/
cd ~/Submit-MM && git add plugin.json && git commit -m "Sync plugin.json" && git push
```
Alleen nodig als er modules zijn bijgekomen of het versienummer is veranderd.

### Stap 2 — Tag aanmaken en pushen
```bash
cd ~/Submit-MM
git tag -a v1.2 -m "Release omschrijving"
git push origin v1.2
```
De tag is je eigen versienummer (v1.2, v1.3, etc.) — dit staat los van de bestandsnaam.

### Stap 3 — Workflow starten op GitHub
1. Ga naar: github.com/submitaudio/submit-metamodule/actions
2. Klik op "Build and release MetaModule plugin"
3. Klik rechts op "Run workflow"
4. Selecteer bij "Use workflow from": Tags → kies jouw tag
5. SDK branch: main
6. Vink "Create Release" aan
7. Klik "Run workflow"

### Stap 4 — Controleren
- Wacht 3-5 minuten
- Groen = release staat op: github.com/submitaudio/submit-metamodule/releases
- Download `Submit-v2.1.mmplugin` en test op MetaModule hardware

---

## Tag verwijderen en opnieuw aanmaken
Als er iets fout ging en je de tag opnieuw wil aanmaken:
```bash
git tag -d v1.2
git push origin :refs/tags/v1.2
git tag -a v1.2 -m "Release omschrijving"
git push origin v1.2
```

---

## SDK versie upgrade
Als de MetaModule SDK een nieuwe versie krijgt (bijv. v2.2):
1. Update de bestandsnaam in `.github/workflows/build-metamodule-plugin.yml`
   - Zoek: `Submit-v2.1.mmplugin`
   - Vervang door: `Submit-v2.2.mmplugin`
2. Update de SDK branch in de workflow indien nodig
3. Commit, push en maak nieuwe release

---

## Workflow permissions instellen (eenmalig)
Ga naar: github.com/submitaudio/submit-metamodule/settings/actions
- Scroll naar "Workflow permissions"
- Zet op "Read and write permissions"
- Sla op

---

## Relatie met VCV plugin
| | VCV | MetaModule |
|---|---|---|
| Repo | submit-vcv-modules | submit-metamodule |
| Build | Automatisch bij push | Handmatig via Actions + tag |
| Bestandsnaam | submit-VERSION-OS.zip | Submit-v2.1.mmplugin |
| Versie in naam | Plugin versie | SDK versie |
