#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

namespace hbs {
struct ArchiveSection { uint32_t id; std::vector<uint8_t> bytes; };
}

#include "../modules/BattleCodec.inc.cpp"

static int g_failures = 0;

static void Expect(bool condition, const char* name)
{
    if (!condition) {
        printf("FAIL %s\n", name);
        ++g_failures;
    }
}

int main()
{
    CodecCapture capture;
    memset(&capture, 0, sizeof(capture));
    capture.version = 1;
    capture.turn = 7;
    capture.currentMonSide = 1;
    capture.currentMonIndex = 3;
    capture.spellPoints[0] = 12;
    capture.spellPoints[1] = -4;
    capture.rngTlsSeed = 0x12345678;
    capture.rngMirrorSeed = 0x89ABCDEF;
    capture.fortWallsHp[17] = 900;
    capture.stacks[0][2].occupied = true;
    capture.stacks[0][2].type = 45;
    capture.stacks[0][2].numberAlive = 18;
    capture.stacks[0][2].healthLost = 9;
    capture.stacks[0][2].activeSpellDuration[5] = 3;
    capture.stacks[0][2].spellIds.push_back(5);
    capture.stacks[0][2].spellIds.push_back(80);
    capture.stacks[0][2].relations[2].push_back(CodecIdentity{1, 4});
    capture.squares[186].deadStacksNumber = 2;
    capture.squares[186].deadStackSide[1] = 1;
    capture.squares[186].deadStackIndex[1] = 20;
    capture.obstacles.push_back(CodecObstacle{3, 22, -1, 1, 50, 2, 0});
    capture.logLines.push_back(std::string("line-a"));
    capture.logLines.push_back(std::string(300, 'x'));
    capture.eagleEye[1].push_back(42);

    std::vector<hbs::ArchiveSection> sections;
    std::string error;
    Expect(CodecEncode(capture, &sections, &error), "encode");
    CodecCapture decoded;
    Expect(CodecDecode(sections, &decoded, &error), "decode");
    Expect(decoded.turn == 7 && decoded.currentMonIndex == 3, "battle scalars");
    Expect(decoded.spellPoints[1] == -4, "signed spell points");
    Expect(decoded.fortWallsHp[17] == 900, "fort hp");
    Expect(decoded.stacks[0][2].numberAlive == 18, "stack count");
    Expect(decoded.stacks[0][2].activeSpellDuration[5] == 3, "spell duration");
    Expect(decoded.stacks[0][2].spellIds.size() == 2 && decoded.stacks[0][2].spellIds[1] == 80, "spell ids");
    Expect(decoded.stacks[0][2].relations[2].size() == 1 && decoded.stacks[0][2].relations[2][0].slot == 4, "relations");
    Expect(decoded.squares[186].deadStackIndex[1] == 20, "square corpses");
    Expect(decoded.obstacles.size() == 1 && decoded.obstacles[0].featureDamage == 50, "obstacles");
    Expect(decoded.logLines.size() == 2 && decoded.logLines[1].size() == 300, "full log line");

    capture.stacks[1][0].spellIds.push_back(81);
    Expect(!CodecEncode(capture, &sections, &error), "reject invalid spell id");
    capture.stacks[1][0].spellIds.clear();
    capture.logLines.push_back(std::string(70000, 'y'));
    Expect(!CodecEncode(capture, &sections, &error), "reject overlong log");

    if (g_failures) {
        printf("%d codec test(s) failed\n", g_failures);
        return 1;
    }
    printf("codec tests passed\n");
    return 0;
}
