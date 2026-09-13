#pragma once

namespace Menu
{
	// Must run at kDataLoaded: the framework header caches its exports on first use, so calling earlier can cache nullptr for good.
	void Register();

	void Refresh();
}
