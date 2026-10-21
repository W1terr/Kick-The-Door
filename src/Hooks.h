#pragma once

namespace Hooks
{
	void Install();      // at plugin load: the player update and skeleton hooks
	void InstallInput(); // after the input devices exist (data loaded)
}
