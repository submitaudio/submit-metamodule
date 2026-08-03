# Submit MetaModule — Release Workflow

## Bestandsnaam
De plugin gebruikt altijd de actuele pluginversie:

`Submit-v<releaseversie>.mmplugin`

Voor versie 2.17.0 is dit `Submit-v2.17.mmplugin`.

- De pluginversie komt uit `plugin.json` en `plugin-mm.json`.
- De SDK-versie staat in het pakket en hoeft bij stabiele firmware niet in de bestandsnaam.
- De releaseworkflow haalt een eventuele voorloop-`v` automatisch uit de Git-tag.

---

## Release maken

### Stap 1 — plugin.json synchroniseren
Controleer of `plugin.json` in Submit-MM up-to-date is met de VCV versie:
```bash
cp ~/Submit/plugin.json ~/Submit-MM/
cd ~/Submit-MM && git add plugin.json && git commit -m "Sync plugin.json" && git push
```
Alleen nodig als er modules zijn bijgekomen of het versienummer is veranderd.

Controleer ook altijd `plugin-mm.json`:
```json
{
  "MetaModuleBrandName": "Submit",
  "version": "2.17.0",
  "MetaModuleIncludedModules": []
}
```

`MetaModuleIncludedModules` moet de volledige lijst bevatten van alle modules
die werkelijk in de MetaModule-plugin worden gebouwd, zoals vereist door de
officiële MetaModule-releasehandleiding.

### Stap 2 — Releaseversie committen
Als de MetaModule releaseversie verandert, update dan:
- `plugin.json`
- `plugin-mm.json`

Daarna committen en pushen:
```bash
cd ~/Submit-MM
git add plugin.json plugin-mm.json
git commit -m "Release MetaModule plugin 2.17.0"
git push origin master
```

### Stap 3 — Tag aanmaken en pushen
```bash
cd ~/Submit-MM
git tag v2.17.0
git push origin v2.17.0
```
De tag is de pluginversie en bepaalt ook de versie in de bestandsnaam.

### Stap 4 — Workflow starten op GitHub
1. Ga naar: github.com/submitaudio/submit-metamodule/actions
2. Klik op "Build and release MetaModule plugin"
3. Klik rechts op "Run workflow"
4. Selecteer bij "Use workflow from": Tags → kies jouw tag
5. SDK branch: main
6. Vink "Create Release" aan
7. Klik "Run workflow"

### Stap 5 — Controleren
- Wacht 3-5 minuten
- Groen = release staat op: github.com/submitaudio/submit-metamodule/releases
- Download `Submit-v2.17.mmplugin` en test op MetaModule hardware

---

## Tag verwijderen en opnieuw aanmaken
Als er iets fout ging en je de tag opnieuw wil aanmaken:
```bash
git tag -d v2.17.0
git push origin :refs/tags/v2.17.0
git tag v2.17.0
git push origin v2.17.0
```

---

## SDK versie upgrade
Als de MetaModule SDK een nieuwe stabiele versie krijgt:
1. Update de SDK branch of tag in de workflow indien nodig.
2. Verhoog ook de pluginversie, zodat gebruikers releases kunnen onderscheiden.
3. Vermeld alleen bij developmentfirmware expliciet `-dev-Z` in de bestandsnaam.
4. Commit, push en maak een nieuwe release.

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
| Bestandsnaam | submit-VERSION-OS.zip | Submit-vVERSION.mmplugin |
| Versie in naam | Plugin versie | Plugin versie |
