
// LuaUser.cpp

// Implements the user-specified (user == MCServer here) functions that the Lua engine uses
// Used for counting the potential mutex locks if multithreading was added to the Lua engine

extern "C"
{
	#include "lauxlib.h"
	#include "lualib.h"
}

#include "LuaUser.h"
#include <atomic>





// The counter for the lock operations
#ifdef _WIN32
	__declspec(dllexport) std::atomic_size_t g_NumLocks(0);
#else
	std::atomic_size_t g_NumLocks(0);
#endif





void LuaLock(lua_State * L)
{
	++g_NumLocks;
}





void LuaUnlock(lua_State * L)
{
	// Nothing needed yet
}




