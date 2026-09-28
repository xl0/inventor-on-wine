/* MSVC-EH half of cxx_catch_throw.c: build with
 * clang -target x86_64-pc-windows-msvc -O1 -fexceptions -fcxx-exceptions -c */
extern "C" void __stdcall OutputDebugStringA(const char *);
struct E { int v; };
struct Guard { ~Guard() { OutputDebugStringA("~Guard\n"); } };

__declspec(noinline) static void thrower(int v) { throw E{v}; }

__declspec(noinline) static void rethrower(int v)
{
    Guard g;
    try { thrower(v); }
    catch (...) { throw; }
}

/* mode 0: plain throw, no catch here; 1: catch + rethrow (throw;);
 * 2: catch + throw a new object; 3: catch + throw from a callee;
 * 4: catch + a callee that catches and rethrows (nested catch blocks) */
__declspec(noinline) static void inner(int mode)
{
    Guard g;
    if (mode == 0) thrower(1);
    try { thrower(1); }
    catch (E &e)
    {
        if (mode == 1) throw;
        if (mode == 2) throw E{e.v + 10};
        if (mode == 3) thrower(e.v + 20);
        rethrower(e.v + 30);
    }
}

extern "C" void cxx_throw(int mode)
{
    inner(mode);
}

/* same, caught by a native (compiler) __except around it */
extern "C" int cxx_throw_seh(int mode)
{
    __try { inner(mode); }
    __except (1) { return 1; }
    return 0;
}

/* caught by a C++ catch in the caller */
extern "C" int cxx_throw_catch(int mode)
{
    try { inner(mode); }
    catch (E &e) { return e.v; }
    return 0;
}
