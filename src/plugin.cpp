#include "plugin.hpp"

Plugin* pluginInstance;

void init(Plugin* p) {
	pluginInstance = p;

	// Add all Models defined throughout the plugin
	p->addModel(modelVocalLamma);
}
