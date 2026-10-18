// Run the production cheat service against synthetic guest memory. No game files.
#include "runtime.h"
#include "mods/mods.h"
#include "input.h"
#include <array>
#include <cassert>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
void log_msg(const char*, ...) {}
namespace ss { void notice(const std::string&) {} }
namespace true60 { float test_dt = 1; float dt() { return test_dt; } }
uint32_t guest_call(Cpu* cpu, uint32_t address, std::initializer_list<uint32_t> args) {
    assert(address == 0x023F6564 && args.size() == 2);
    const uint32_t link = *args.begin();
    st32(link + 0x65F0, 0x27);
    st32(link + 0x6A70, 2);
    cpu->r[3] = 0xBAD; // The hook must preserve the caller's registers.
    return 0;
}
static void map(uint32_t address, size_t size) {
#ifdef _WIN32
    auto p = VirtualAlloc(mem::ptr(address), size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    auto p = mmap(mem::ptr(address), size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
    assert(p == mem::ptr(address));
}
int main() {
    map(0x101F0000, 0x10000);
    map(0x10470000, 0x10000);
    constexpr uint32_t base = 0x15000000, player = base + 0x20;
    map(base, 0x10000);
    st32(0x101F84DC, base);
    std::memcpy(mem::ptr(0x104741E4), "Atorizk", 8);
    // Each earned sword phase, including before Medli and between the two temples.
    for (uint8_t sword : {1, 3, 7, 15}) {
        std::memset(mem::ptr(base), 0xA5, 0x2000);
        st16(player, 14); st16(player + 2, 14); st16(player + 4, 158);
        st8(player + 0x12, 0);
        std::memcpy(mem::ptr(player + 0x30), "sea\0\0\0\0\0", 8);
        st8(player + 0xB4, sword); st8(player + 0xB5, 1);
        std::array<uint8_t, 0x2000> expected;
        std::memcpy(expected.data(), mem::ptr(base), expected.size());
        expected[0x2E] = 0x3E; expected[0x2F] = 0x3C;
        mods::request_cheat(mods::kCheatSword);
        mods::cheats_service();
        // Includes all story flags, ownership bits, adjacent memory, and the save header.
        assert(!std::memcmp(expected.data(), mem::ptr(base), expected.size()));
        mods::cheats_service();
        assert(!std::memcmp(expected.data(), mem::ptr(base), expected.size()));
    }
    constexpr uint32_t link = 0x16000000, camera = 0x17000000;
    map(link, 0x10000); map(camera, 0x10000);
    Cpu cpu{}; cpu.r[3] = link;
    float x = 7, z = 9;
    // A disabled cheat must not read an unmapped actor or alter output arguments.
    mods::moon_jump_step(&cpu, 0x18000000, true);
    assert(!mods::moon_jump_move(0x18000000, x, z) && x == 7 && z == 9);
    mods::set_infinite(mods::kMoonJump, true);
    mods::moon_jump_step(&cpu, link, true); // consume the switch's reset request
    mods::moon_jump_step(&cpu, link, false);
    input::PadState pad{}; pad.buttons = input::kL; pad.lx = -1;
    mods::moon_jump_input(pad);
    stf32(camera + 0xF0, 1); // camera facing +Z
    mods::moon_jump_camera(camera);
    st32(link + 0x834, 0xFFFF); stf32(link + 0x318, 100);
    stf32(link + 0x6A48, 999);
    mods::moon_jump_step(&cpu, link, true);
    assert(cpu.r[3] == link && ld32(link + 0x65F0) == 0x27);
    assert((ld32(link + 0x834) & 0xE0) == 0 && ldf32(link + 0x340) == 25);
    assert(ldf32(link + 0x6A48) == 999); // optional fall protection is off
    assert(mods::moon_jump_move(link, x, z) && ld16(link + 0x322) == 3000);
    mods::moon_jump_step(&cpu, link, false);
    std::array<uint8_t, 0x7000> full_actor;
    std::memcpy(full_actor.data(), mem::ptr(link), full_actor.size());
    true60::test_dt = .5f;
    mods::moon_jump_step(&cpu, link, true);
    assert(mods::moon_jump_move(link, x, z) && ld16(link + 0x322) == 4500);
    mods::moon_jump_step(&cpu, link, false);
    // Simulate true60's guest actor rollback; the host turn must also roll back.
    std::memcpy(mem::ptr(link), full_actor.data(), full_actor.size());
    true60::test_dt = 1;
    mods::moon_jump_step(&cpu, link, true);
    assert(mods::moon_jump_move(link, x, z) && ld16(link + 0x322) == 6000);
    mods::moon_jump_step(&cpu, link, false);
    mods::set_infinite(mods::kMoonNoFall, true);
    pad.buttons = 0; mods::moon_jump_input(pad);
    mods::moon_jump_step(&cpu, link, true);
    assert(ldf32(link + 0x6A48) == 100);
    mods::moon_jump_step(&cpu, link, false);
    st32(link + 0x6A70, 0); // landing ends steering and fall protection
    mods::moon_jump_step(&cpu, link, true);
    assert(!mods::moon_jump_move(link, x, z));
    mods::moon_jump_step(&cpu, link, false);
    stf32(link + 0x6A48, 777);
    mods::moon_jump_step(&cpu, link, true);
    mods::moon_jump_step(&cpu, link, false);
    assert(ldf32(link + 0x6A48) == 777); // ordinary falls are not protected
    // Even when the allocator reuses Link's address, a scene change invalidates
    // the old camera until the new scene has drawn one.
    std::memcpy(mem::ptr(0x104741E4), "Other\0\0\0", 8);
    pad.buttons = input::kL; mods::moon_jump_input(pad);
    mods::moon_jump_step(&cpu, link, true);
    assert(mods::moon_jump_move(link, x, z) && x == 0 && z == 0);
    mods::moon_jump_step(&cpu, link, false);
    mods::moon_jump_camera(camera);
    mods::moon_jump_step(&cpu, link, true);
    assert(mods::moon_jump_move(link, x, z) && (x != 0 || z != 0));
    mods::moon_jump_step(&cpu, link, false);
    auto invalid_transition = [&](bool title_draw) {
        std::memcpy(mem::ptr(0x104741E4), "Name\0\0\0\0", 8);
        mods::moon_jump_step(&cpu, link, true);
        mods::moon_jump_step(&cpu, link, false);
        if (title_draw) mods::moon_jump_camera(camera);
        std::memcpy(mem::ptr(0x104741E4), "Other\0\0\0", 8);
        mods::moon_jump_input(pad);
        mods::moon_jump_step(&cpu, link, true);
        assert(mods::moon_jump_move(link, x, z) && x == 0 && z == 0);
        mods::moon_jump_step(&cpu, link, false);
        mods::moon_jump_camera(camera);
        mods::moon_jump_step(&cpu, link, true);
        assert(mods::moon_jump_move(link, x, z) && (x != 0 || z != 0));
        mods::moon_jump_step(&cpu, link, false);
    };
    invalid_transition(false);
    invalid_transition(true);
    mods::moon_jump_reset();
    assert(!mods::moon_jump_move(link, x, z));
    mods::moon_jump_input(pad);
    mods::moon_jump_camera(camera); // valid same-scene capture before initial execute
    mods::moon_jump_step(&cpu, link, true);
    assert(mods::moon_jump_move(link, x, z) && (x != 0 || z != 0));
    mods::moon_jump_step(&cpu, link, false);
    mods::set_infinite(mods::kMoonJump, false);
    mods::moon_jump_step(&cpu, 0x18000000, true);
}
