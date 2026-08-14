# Submit Audio MetaModule

Deze repository is de actieve bron van waarheid voor Submit Audio MetaModule-plugins.

## Zichtbare taal — altijd Engels

Alle tekst die de gebruiker in een Submit Audio-module ziet, is altijd Engels. Dit geldt voor alle VCV Rack- en MetaModule-modules, inclusief panelteksten, displays, contextmenu's, parameter- en poortnamen, statusmeldingen, foutmeldingen en helpteksten. Nederlandse tekst is alleen toegestaan in gesprekken met José, interne documentatie en niet-zichtbare broncodecommentaren.

## Clockstandaard — 1 PPQN

Gebruik voor Submit Audio-modules altijd **1 PPQN** als standaardclock:

- 1 PPQN betekent één puls per kwartnoot.
- Een `×1`-clockuitgang levert één puls per kwartnoot.
- Snellere ritmische stappen, zoals achtsten of zestienden, worden intern door de ontvangende module afgeleid.
- Gebruik 4 PPQN niet als ongemarkeerde standaard. Alleen een expliciete legacy- of compatibiliteitsmodus mag hiervan afwijken.
- Bewaar bij wijzigingen aan bestaande modules altijd patchcompatibiliteit; bestaande afwijkende clockmodi mogen niet stilzwijgend van snelheid veranderen.
- Houd deze standaard gelijk aan de VCV Rack-versie.

## Verplichte conversiehandleiding

Lees vóór elke nieuwe conversie of layoutaanpassing van VCV Rack naar MetaModule altijd volledig:

`VCV_TO_METAMODULE.md`

Lees vóór paneel-, component-, custom-knop-, SDK- of simulatorassetwerk ook altijd volledig:

`METAMODULE_WORKFLOW.md`

Volg die handleiding ook wanneer een bestaande MetaModule-module opnieuw wordt uitgelijnd, wanneer een faceplate wordt vervangen of wanneer componentposities worden aangepast.

## Scheiding van repositories

- VCV Rack bron: `/Users/studio67/SubmitAudio-Development/Projects/VCV-Rack`
- MetaModule bron: `/Users/studio67/SubmitAudio-Development/Projects/MetaModule`
- MetaModule simulator: `/Users/studio67/SubmitAudio-Development/Toolchains/MetaModule-Platform/simulator`

Wijzig VCV Rack en MetaModule niet tegelijk, tenzij José dat expliciet vraagt.

## Testregel

Een MetaModule-aanpassing is pas klaar nadat de plugin en simulator opnieuw zijn gebouwd en de module in een echte simulatorpatch is gecontroleerd.

Start de simulator standaard met audio-uitgang 1:

```sh
cd /Users/studio67/SubmitAudio-Development/Toolchains/MetaModule-Platform/simulator
./build/simulator --audioout 1
```
