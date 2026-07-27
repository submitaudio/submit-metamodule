# Submit Audio MetaModule

Deze repository is de actieve bron van waarheid voor Submit Audio MetaModule-plugins.

## Verplichte conversiehandleiding

Lees vóór elke nieuwe conversie of layoutaanpassing van VCV Rack naar MetaModule altijd volledig:

`VCV_TO_METAMODULE.md`

Volg die handleiding ook wanneer een bestaande MetaModule-module opnieuw wordt uitgelijnd, wanneer een faceplate wordt vervangen of wanneer componentposities worden aangepast.

## Scheiding van repositories

- VCV Rack bron: `/Users/studio67/Submit`
- MetaModule bron: `/Users/studio67/Submit-MM`
- MetaModule simulator: `/Users/studio67/metamodule-main-git/simulator`

Wijzig VCV Rack en MetaModule niet tegelijk, tenzij José dat expliciet vraagt.

## Testregel

Een MetaModule-aanpassing is pas klaar nadat de plugin en simulator opnieuw zijn gebouwd en de module in een echte simulatorpatch is gecontroleerd.

Start de simulator standaard met audio-uitgang 1:

```sh
cd /Users/studio67/metamodule-main-git/simulator
./build/simulator --audioout 1
```
