
// LuaUser.h

// Declares the user-specified (user == MCServer here) functions that the Lua engine uses
// Used for counting the potential mutex locks if multithreading was added to the Lua engine




void LuaLock(lua_State * L);
void LuaUnlock(lua_State * L);




