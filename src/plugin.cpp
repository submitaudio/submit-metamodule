#include "plugin.hpp"
#include "CoreModules/register_module.hh"

#ifdef METAMODULE_BUILTIN
extern Plugin* pluginInstance;
#else
Plugin* pluginInstance;
#endif

#ifdef METAMODULE_BUILTIN
void init_Submit(Plugin* p) {
#else
void init(Plugin* p) {
#endif
    pluginInstance = p;
    p->addModel(modelDrift);
    p->addModel(modelImpact);
    p->addModel(modelChrono);
    p->addModel(modelChain);
    p->addModel(modelSqueeze);
    p->addModel(modelShape);
    p->addModel(modelMaster);
    p->addModel(modelGain);
    p->addModel(modelSweep);
    p->addModel(modelLoop);
}
