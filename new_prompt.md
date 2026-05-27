

Doom95 was relying on the SDL event pump being called from another thread however it cause instabilities.
How can we keep pumping events agnostically fdrom the binary without breaking samples. Doom95 don't use the normal event methods so the current imlplementation doesn't work. We were relying on a timGetTime method as doom is calling it frequently but it is not future proof. What are our options?