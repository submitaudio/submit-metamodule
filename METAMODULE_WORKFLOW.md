# Submit Audio MetaModule-werkwijze

Dit document beschrijft de vaste werkwijze voor het bouwen en restylen van
Submit Audio-modules voor MetaModule. De actieve MetaModule-repository is
`/Users/studio67/SubmitAudio-Development/Projects/MetaModule`.

## 1. Bronnen en scheiding

- VCV Rack-code en assets: `/Users/studio67/SubmitAudio-Development/Projects/VCV-Rack`
- Actieve MetaModule-code en afgeleide assets: `/Users/studio67/SubmitAudio-Development/Projects/MetaModule`
- MetaModule Plugin SDK: `/Users/studio67/SubmitAudio-Development/Toolchains/MetaModule-Plugin-SDK`
- Simulator: `/Users/studio67/SubmitAudio-Development/Toolchains/MetaModule-Platform/simulator`
- De simulator gebruikt `/Users/studio67/SubmitAudio-Development/Projects/MetaModule` via `ext-plugins.cmake`.
- Gebruik `/Users/studio67/SubmitAudio-Development/Projects/VCV-Rack/metamodule` niet voor actief MetaModule-werk.
- Wijzig VCV Rack en MetaModule niet tegelijk, tenzij José dat expliciet vraagt.

## 2. Originele ontwerpbestanden

De centrale ontwerpmap is:

`/Users/studio67/Library/CloudStorage/Dropbox/01 Klanten 2015/Submit/VCV Rack panel ontwerpen`

De originele Submit-knoppen staan altijd in:

`/Users/studio67/Library/CloudStorage/Dropbox/01 Klanten 2015/Submit/VCV Rack panel ontwerpen/Interface styling/Knobs`

Beschikbare bronbestanden:

- `SubmitKnobLarge.svg`
- `SubmitKnobMedium.svg`
- `SubmitKnobCompact.svg`
- `SubmitKnobSmall.svg`
- `SubmitKnobTiny.svg`
- `SubmitKnobMini.svg`

Gebruik deze SVG's als bron. Bewaar de naar MetaModule omgerekende PNG-assets in
`/Users/studio67/SubmitAudio-Development/Projects/MetaModule/assets`. Wijzig de originele ontwerpbestanden niet
vanuit de pluginrepository.

Per module staan de panel- en componentbestanden in een eigen submap van de
centrale ontwerpmap, bijvoorbeeld `Drift/`. Gebruik:

- `Panel-design-<Module>-...` voor het paneel;
- `Panel-components-<Module>-...` of de aangeleverde variant voor posities en maten.

Let op dat historische bestandsnamen typefouten kunnen bevatten, zoals
`Panel-componenets-...`. Gebruik altijd het werkelijk aangeleverde bestand en
controleer het volledige pad.

## 3. Wat José aanlevert

Voor een nieuwe MetaModule-faceplate zijn bij voorkeur aanwezig:

1. een definitief MetaModule-paneel als PNG;
2. het bijbehorende component-SVG met de oorspronkelijke VCV-maten;
3. indien nodig de module-specifieke originele SVG-assets.

Een paneel-PNG voor MetaModule is normaal exact 240 pixels hoog. Bereken de
breedte proportioneel. Rek het paneel nooit alleen horizontaal of verticaal uit.

De bestaande vierkante buttons, switches, sliders, leds en poorten blijven
behouden wanneer José alleen om nieuwe ronde knoppen vraagt.

## 4. Verplichte uniforme schaal

Een component-SVG mag op VCV-schaal worden aangeleverd. Reken het altijd met één
uniforme schaalfactor om naar de MetaModule-faceplate:

```text
schaalfactor = hoogte MetaModule-paneel / hoogte viewBox component-SVG
meta_x       = bron_x * schaalfactor
meta_y       = bron_y * schaalfactor
meta_breedte = bron_breedte * schaalfactor
meta_hoogte  = bron_hoogte * schaalfactor
```

Gebruik dezelfde factor voor X, Y, breedte, hoogte en straal. Gebruik nooit
verschillende X- en Y-factoren.

### Drift-referentie

Voor Drift heeft het component-SVG een `viewBox` van ongeveer
`526.817 × 380` en is het MetaModule-paneel `333 × 240` pixels:

```text
schaalfactor = 240 / 380 = 0.631578947
```

Daarmee zijn de gebruikte ronde MetaModule-knoppen:

- Large: `49 × 49 px`
- Medium: `34 × 34 px`
- Small: `20 × 20 px`

Rond pas aan het einde naar hele pixels af. Controleer daarna visueel of iedere
knop exact binnen de getekende schaallijnen valt.

De uniforme MetaModule-schaalfactor hierboven bepaalt de afmetingen van de
gerenderde PNG-assets. Gebruik die factor niet nogmaals voor widgetposities in
Rack-code die `mm2px(...)` gebruikt. Lees voor zulke widgets het middelpunt
`cx/cy` rechtstreeks uit het component-SVG en reken exact om met:

```text
x_mm = cx * 25.4 / 75
y_mm = cy * 25.4 / 75
```

Gebruik voor alle ronde parameters `createParamCentered(...)` en voor ronde
aansluitingen altijd `createInputCentered(...)` of `createOutputCentered(...)`.
Pas dezelfde `cx/cy × 25.4 / 75`-regel toe op knoppen, poorten en leds. Rond de
berekende millimeterposities pas in de broncode af op minimaal drie decimalen.
Handmatig overnemen of visueel corrigeren is niet toegestaan zolang een
component-SVG met betrouwbare `cx/cy`-waarden beschikbaar is.

Als de bestaande modulecode al dezelfde bewezen millimeter- of
pixelcoördinaten gebruikt als het component-SVG, verschuif de middelpunten niet.
Schaal dan alleen de afgeleide visuele knopassets naar de MetaModule-maat.

## 5. PNG-export van knoppen

- Exporteer met transparante achtergrond.
- Gebruik een strak bijgesneden canvas zonder extra transparante marge.
- Behoud de beeldverhouding.
- Gebruik exact de berekende pixelmaat.
- Controleer breedte, hoogte en alfakanaal na export.
- Gebruik voor verschillende maten afzonderlijke PNG-bestanden.

Voor Drift zijn de afgeleide assets bijvoorbeeld:

- `assets/SubmitKnobLarge.png`
- `assets/SubmitKnobMedium.png`
- `assets/SubmitKnobSmall.png`

De componentpositie komt uit het component-SVG of uit de al bewezen VCV-code;
de PNG bepaalt uitsluitend het uiterlijk en de werkelijke visuele maat.

## 6. Custom knoppen in de modulecode

Gebruik voor de nieuwe ronde Submit-knoppen een kleine `SvgKnob`-klasse die de
PNG uit de pluginassets laadt. Houd de normale parameterhoek aan en schakel de
standaardschaduw uit wanneer deze niet bij het ontwerp hoort.

Voorbeeld:

```cpp
template <const char* AssetPath>
struct SubmitKnob : SvgKnob {
    SubmitKnob() {
        minAngle = -0.83 * M_PI;
        maxAngle =  0.83 * M_PI;
        setSvg(Svg::load(asset::plugin(pluginInstance, AssetPath)));
        shadow->opacity = 0.f;
    }
};
```

Gebruik daarna `createParamCentered<...>()` met het bewezen middelpunt. Verander
geen parameter-ID, bereik, defaultwaarde of patchopslag bij een zuivere restyle.

## 7. Paneelasset

- Plaats de definitieve MetaModule-PNG in `/Users/studio67/SubmitAudio-Development/Projects/MetaModule/assets`.
- Gebruik de bestaande modulebestandsnaam die de MetaModule-assetmapping verwacht.
- Controleer hoofdletters exact.
- Houd VCV Rack- en MetaModule-faceplates gescheiden.
- Vervang nooit automatisch het VCV-paneel door de speciale MetaModule-versie.

Na iedere wijziging aan paneel of knoppen moet de simulatorasset-cache opnieuw
worden opgebouwd; alleen de C++-build vernieuwen is niet voldoende.

## 8. SDK-versie: nieuwste stabiele versie, exact vastgezet

Gebruik voor nieuwe productie- en releasebuilds de nieuwste stabiele officiële
4ms MetaModule Plugin SDK. Controleer de versie altijd opnieuw en zet voor een
release een exacte tag of commit vast; gebruik geen ongemerkt zwevende `main`.

Op het moment van schrijven is de nieuwste officiële stabiele tag:

```text
api-v2.3.0
commit 06b26d2cca5af36ef6456f6685ce0c169af56cb9
```

Controleer lokaal vóór configureren:

```sh
git -C /Users/studio67/SubmitAudio-Development/Toolchains/MetaModule-Plugin-SDK describe --tags --always --dirty
git -C /Users/studio67/SubmitAudio-Development/Toolchains/MetaModule-Plugin-SDK rev-parse HEAD
git -C /Users/studio67/SubmitAudio-Development/Toolchains/MetaModule-Platform/firmware/metamodule-plugin-sdk describe --tags --always --dirty
git -C /Users/studio67/SubmitAudio-Development/Toolchains/MetaModule-Platform/firmware/metamodule-plugin-sdk rev-parse HEAD
```

De plugin-SDK en simulator-SDK moeten voor de test bewust op dezelfde stabiele
API-versie staan. Stop wanneer ze niet overeenkomen; configureer en bouw dan niet
alsof de migratie al voltooid is.

Houd SDK-migratie en DSP-wijzigingen als afzonderlijke stappen. Maak eerst een
functionele en CPU-baseline met ongewijzigde DSP op de nieuwe SDK.

## 9. Plugin bouwen

Configureer na een SDK-wijziging altijd opnieuw met het expliciete SDK-pad:

```sh
cd /Users/studio67/SubmitAudio-Development/Projects/MetaModule
cmake --fresh -B build -GNinja \
  -DMETAMODULE_SDK_DIR=/Users/studio67/SubmitAudio-Development/Toolchains/MetaModule-Plugin-SDK
cmake --build build --target plugin -j4
```

Zonder SDK-wijziging volstaat normaal:

```sh
cd /Users/studio67/SubmitAudio-Development/Projects/MetaModule
cmake --build build --target plugin -j4
```

De build is alleen geslaagd wanneer de plugin wordt gemaakt en de symbolcheck
eindigt met `All symbols found!`.

## 10. Simulator en asset-cache

De simulator gebruikt `/Users/studio67/SubmitAudio-Development/Projects/MetaModule` via `ext-plugins.cmake`.
Configureer hem na een SDK-update opnieuw. Bouw na broncodewijzigingen opnieuw.

```sh
cd /Users/studio67/SubmitAudio-Development/Toolchains/MetaModule-Platform/simulator
cmake --fresh -B build -GNinja
cmake --build build -j4
```

Na panel-, knop-, font- of andere assetwijzigingen moeten de gekopieerde assets
en de image-cache worden vernieuwd voordat de visuele test betrouwbaar is:

```sh
cd /Users/studio67/SubmitAudio-Development/Toolchains/MetaModule-Platform/simulator
cmake -E remove_directory build/assets
cmake -E rm -f build/assets.uimg build/assets.uimg.tar
cmake --build build --target asset-image -j4
```

Start altijd met audio-uitgang 1:

```sh
cd /Users/studio67/SubmitAudio-Development/Toolchains/MetaModule-Platform/simulator
./build/simulator --audioout 1
```

Controleer dat slechts één actuele simulatorinstantie draait.

## 11. Verplichte visuele en functionele controle

Een module is pas klaar na controle in een echte simulatorpatch:

- het juiste paneel verschijnt zonder vervorming;
- iedere ronde knop valt zuiver binnen de schaallijnen;
- knoppen draaien rond hun eigen middelpunt;
- vierkante buttons, switches en sliders zijn niet onbedoeld veranderd;
- leds, inputs en outputs staan exact op hun markeringen;
- alle bediening reageert;
- audio en CV werken zonder duidelijke glitches;
- er ontbreken geen assets en er worden geen verkeerde paden gebruikt;
- plugin- en simulatorbuild zijn geslaagd;
- relevante CPU-belasting en pieken zijn gecontroleerd;
- daarna volgt nog een test op echte MetaModule-hardware.

Voer tot slot `git diff --check`, `git status --short` en `git diff` uit. Commit of
push niets zonder José's expliciete opdracht.
