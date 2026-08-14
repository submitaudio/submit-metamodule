# VCV Rack naar MetaModule

Deze handleiding is verplicht voor iedere Submit Audio-conversie van VCV Rack naar MetaModule. Het doel is dat panel, bediening, DSP en assets bij de eerste simulatorbuild correct overeenkomen met de VCV-module.

Lees daarnaast vóór panel-, component- of custom-knopwerk altijd volledig:

`METAMODULE_WORKFLOW.md`

Dat document is de actuele praktische bron voor de centrale ontwerpbestanden,
Submit-knoppen, uniforme VCV-naar-MetaModule-schaal, paneelexport, SDK-pinning,
builds en simulatorasset-cache.

## Bronnen

- Gebruik `/Users/studio67/SubmitAudio-Development/Projects/VCV-Rack` als bron voor de actuele VCV Rack-module.
- Bouw de MetaModule-versie in `/Users/studio67/SubmitAudio-Development/Projects/MetaModule`.
- Controleer altijd de modulecode, het panel-SVG en het losse component-SVG voordat posities worden ingevoerd.
- Gebruik `/Users/studio67/SubmitAudio-Development/Projects/VCV-Rack/metamodule` niet voor actief MetaModule-werk.

## Verplichte manifests en modulelijst

Een MetaModule-plugin moet in de root van `/Users/studio67/SubmitAudio-Development/Projects/MetaModule` beide manifestbestanden bevatten:

- `plugin.json`: algemene plugininformatie en de VCV/Rack-modulemetadata die de MetaModule-build nodig heeft.
- `plugin-mm.json`: MetaModule-specifieke informatie voor de officiële pluginlijst.

`plugin-mm.json` moet altijd een volledige `MetaModuleIncludedModules`-lijst bevatten. Neem daarin iedere module op die werkelijk in de MetaModule-plugin wordt gebouwd, met minimaal de exacte `slug` en de zichtbare `name`.

De MetaModule-lijst mag bewust afwijken van de modulelijst in de VCV-plugin. Neem lokale VCV-beta's of niet-geporteerde modules nooit automatisch over. Controleer de lijst daarom tegen `CMakeLists.txt`, `src/plugin.cpp`, `src/plugin.hpp` en de werkelijk meegebouwde modulecode.

Voor een release moeten daarnaast gelden:

- `plugin.json` en `plugin-mm.json` hebben hetzelfde actuele versienummer.
- Iedere MetaModule-slug staat exact één keer in `MetaModuleIncludedModules`.
- Hoofdletters in slugs en namen zijn correct.
- Nieuwe modules zijn niet alleen gebouwd, maar ook aan beide manifests toegevoegd.
- Verwijderde of niet meegeleverde modules staan niet meer in `MetaModuleIncludedModules`.

Wanneer alle modules uit `plugin.json` ook op MetaModule beschikbaar zijn, controleer de lijsten automatisch:

```sh
jq -r '.modules[].slug' plugin.json | sort > /tmp/plugin-slugs.txt
jq -r '.MetaModuleIncludedModules[].slug' plugin-mm.json | sort > /tmp/plugin-mm-slugs.txt
diff -u /tmp/plugin-slugs.txt /tmp/plugin-mm-slugs.txt
```

Een lege `diff` betekent dat beide sluglijsten gelijk zijn. Als de MetaModule-plugin bewust minder modules bevat, beoordeel het verschil handmatig.

## Standaard knoppen

- Gebruik standaard ronde MetaModule-knoppen.
- Gebruik normaal `RoundSmallBlackKnob`, `RoundLargeBlackKnob`, `RoundBigBlackKnob` of `RoundHugeBlackKnob`, passend bij de cirkelmaat in het component-SVG.
- Gebruik de centrale custom Submit-knoppen wanneer José voor de nieuwe Submit-stijl kiest. Volg dan exact de bronlocaties, schaal- en PNG-exportregels uit `METAMODULE_WORKFLOW.md`.
- Gebruik geen andere custom knopafbeeldingen of eigen `SvgKnob`-klassen zonder expliciet overleg.
- Kies de maat op basis van de componentcirkel en controleer het resultaat visueel in de simulator.

## Component-SVG en schaal

Lees alle componenten uit het losse component-SVG:

- draaiknoppen: `cx`, `cy` en `r`
- poorten en leds: `cx`, `cy` en `r`
- schakelaars en sliders: `x`, `y`, `width` en `height`
- displays: positie en volledige rechthoek

Neem altijd eerst de coördinatenmethode van de werkende VCV-code over:

- Gebruikt VCV directe `Vec(x, y)`-waarden, behoud dan exact die VCV-pixelcoördinaten in MetaModule.
- Gebruikt VCV `mm2px(Vec(x, y))`, behoud dan exact die millimetercoördinaten en `mm2px(...)`.
- Meng deze twee methoden niet binnen dezelfde overname zonder de panelmapping eerst te bewijzen.

Belangrijk: Illustrator behandelt pixels vaak als 96 DPI. VCV Rack gebruikt een andere pixelschaal. Reken directe VCV-pixels daarom niet om met `25.4 / 96`; dat plaatst alle elementen te hoog en te ver naar links.

Als omrekening werkelijk nodig is, verifieer dan eerst de fysieke panelmaat en de SVG `viewBox`. Voor standaard VCV Rack-coördinaten geldt als referentie:

```text
VCV pixels per millimeter = 75 / 25.4
millimeter = VCV pixel * 25.4 / 75
```

Directe overname uit de werkende VCV-code heeft altijd de voorkeur boven handmatige omrekening.

## Posities per component

- Gebruik `createParamCentered`, `createInputCentered`, `createOutputCentered` en `createLightCentered` met het middelpunt uit een SVG-cirkel.
- Een rechthoekige schakelaar met `createParam<CKSS>` gebruikt de linkerbovenhoek uit het SVG, niet het middelpunt.
- Gebruik alleen `createParamCentered<CKSS>` wanneer de positie bewust naar het midden van de schakelaar is omgerekend.
- Controleer dat knoppen de schaalverdeling volgen en geen labels bedekken.
- Controleer dat poorten exact in de getekende aansluitingen vallen.
- Controleer leds afzonderlijk; hun positie is niet automatisch gelijk aan de bijbehorende poort.

## Faceplate

- Converteer het definitieve VCV panel-SVG naar een PNG met behoud van de volledige beeldverhouding.
- MetaModule-faceplates zijn normaal 240 pixels hoog; bereken de breedte uit dezelfde verhouding.
- Gebruik de exacte module- en bestandsnaam, inclusief hoofdletters.
- Controleer na assetwijzigingen of de simulatorasset-cache werkelijk opnieuw is gebouwd.

### Definitieve MetaModule-faceplate

Gebruik tijdens de eerste conversie en technische simulator-tests de beschikbare faceplate om DSP, bediening en componentposities te controleren.

Wanneer alles technisch en functioneel is getest, moet Codex José altijd expliciet vragen:

> Wil je nu de nieuwe SVG speciaal voor MetaModule uploaden, met de grotere letters?

Rond de visuele MetaModule-versie pas daarna af:

- vervang de tijdelijke faceplate door José's definitieve MetaModule-SVG
- behoud exact dezelfde paneelverhouding en componentcoördinaten
- converteer de definitieve SVG opnieuw naar de MetaModule-PNG
- vernieuw de simulatorasset-cache
- bouw en test de simulator nog één keer visueel

Gebruik de speciale MetaModule-faceplate niet automatisch voor VCV Rack. De VCV- en MetaModule-assets blijven gescheiden.

## Displays en fonts

- Neem de positie en grootte van de bestaande VCV-displaywidget exact over.
- Gebruik een eigen grafische `Widget` wanneer het display ook in het MetaModule-menu en schermvullend beschikbaar moet zijn.
- Voeg deze widget met `ModuleWidget::addChild()` toe. MetaModule registreert hem dan als dynamisch display onder `Displays` als `Display 1`.
- Controleer dat `Display 1` opent en met het fullscreen-icoon het volledige MetaModule-scherm gebruikt.
- Gebruik `MetaModule::VCVTextDisplay` alleen wanneer tekst uitsluitend op de faceplate nodig is. Dit type wordt niet automatisch opgenomen onder `Displays` en kan niet via dat menu schermvullend worden geopend.

Voor een eigen lettertype:

- Lever het originele `.ttf`- of `.otf`-bestand mee in `/Users/studio67/SubmitAudio-Development/Projects/MetaModule/assets`.
- Lever ook de bijbehorende licentietekst mee.
- Laad het font in de displaywidget met `APP->window->loadFont(asset::plugin(pluginInstance, "res/Fontnaam.ttf"))`.
- Bewaar de geladen `std::shared_ptr<window::Font>` als lid van de widget, zodat het font tijdens tekenen beschikbaar blijft.
- Gebruik bij het tekenen `nvgFontFaceId(args.vg, font->handle)` en voorzie een terugval naar `APP->window->uiFont` wanneer laden mislukt.
- Controleer na de build dat het font werkelijk in `Submit.mmplugin` en in de simulatorassets is opgenomen.
- Controleer lettergrootte, kleur, uitlijning, marges en beschikbare breedte zowel op de faceplate als schermvullend.

De modulebrowser toont alleen een statische preview. Beoordeel dynamische displaytekst altijd nadat de module in een patch is geladen en controleer daarna ook `Displays -> Display 1`.

## Bouwen

### Naam van het releasepakket

De bestandsnaam van een gepubliceerde MetaModule-plugin volgt altijd de actuele
pluginversie uit `plugin.json` en `plugin-mm.json`, niet de SDK-versie.

Gebruik voor Submit dit formaat:

`Submit-v<pluginversie>.mmplugin`

Voor pluginversie 2.16.0 moet de bestandsnaam dus exact zijn:

`Submit-v2.16.0.mmplugin`

De gebruikte MetaModule SDK-versie wordt in het pakket zelf opgenomen en is
geen vervanging voor de pluginversie in de bestandsnaam.

Bouw eerst de MetaModule-plugin:

```sh
cd /Users/studio67/SubmitAudio-Development/Projects/MetaModule
cmake --build build -j4
```

Bouw daarna de simulator opnieuw, omdat deze `/Users/studio67/SubmitAudio-Development/Projects/MetaModule` via `ext-plugins.cmake` gebruikt:

```sh
cd /Users/studio67/SubmitAudio-Development/Toolchains/MetaModule-Platform/simulator
cmake --build build -j4
```

Wanneer panel- of andere assets zijn veranderd, vernieuw ook de simulatorassets voordat je test.

## Simulator

Start altijd met José's audiokaart op uitgang 1:

```sh
cd /Users/studio67/SubmitAudio-Development/Toolchains/MetaModule-Platform/simulator
./build/simulator --audioout 1
```

Wanneer de simulator buiten deze werkmap wordt gestart, bijvoorbeeld met `launchctl`, geef dan altijd het absolute assetpad mee. Zonder dit pad kunnen alle faceplates ontbreken:

```sh
/Users/studio67/SubmitAudio-Development/Toolchains/MetaModule-Platform/simulator/build/simulator \
  --audioout 1 \
  --zoom 100 \
  --assets /Users/studio67/SubmitAudio-Development/Toolchains/MetaModule-Platform/simulator/build/assets.uimg
```

Controleer dat slechts één actuele simulatorinstantie draait, zodat geen oude build of oud venster met de nieuwe versie wordt verward.

## Verplichte eindcontrole

- José: vergeet vóór iedere nieuwe module-release niet de CPU-stresstest te doen.
- Meet CPU-gebruik op MetaModule-hardware met de module los én in een realistische patch.
- Test snelle triggers, maximale ingestelde polyfonie en overlappende stemmen; controleer dat de CPU-meter niet rood wordt.
- Vergelijk na optimalisatie opnieuw met de VCV-versie en luister of klank, dynamiek en functies behouden zijn.
- Module laadt zonder crash of ontbrekende faceplate.
- Panelverhouding en modulebreedte kloppen.
- Alle knoppen hebben de juiste standaardmaat en positie.
- Schakelaar of slider staat exact op de componentlaag.
- Alle inputs, outputs en leds staan exact goed.
- Dynamische displaytekst staat in het display en verandert correct.
- Het gebruikte displayfont is meegebouwd en rendert correct.
- `Displays -> Display 1` is aanwezig voor elk vergrootbaar display.
- Het display werkt ook in fullscreen zonder afgesneden of overlappende tekst.
- Knoppen en schakelaars reageren.
- Inputs en outputs werken.
- Audio en CV lopen zonder duidelijke glitches.
- Pluginbuild en simulatorbuild zijn geslaagd.
- Er is niets gecommit of gepusht zonder José's expliciete opdracht.
