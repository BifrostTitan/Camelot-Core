#include "Camelot/CamelotClient.h"
#include <stdexcept>
#include <iostream>
using namespace camelot;
void check(bool ok, const char *message)
{
    if (!ok)
        throw std::runtime_error(message);
}
void word(Bytes &b, uint16_t n)
{
    b.push_back(uint8_t(n >> 8));
    b.push_back(uint8_t(n));
}
void text(Bytes &b, const std::string &s)
{
    b.push_back(uint8_t(s.size()));
    b.insert(b.end(), s.begin(), s.end());
}
void feed(Client &c, uint8_t op, const Bytes &b, double now = 1)
{
    Bytes f;
    word(f, uint16_t(b.size()));
    f.push_back(op);
    f.insert(f.end(), b.begin(), b.end());
    c.Feed(f.data(), f.size(), now);
}
Bytes payload(const Bytes &b)
{
    return Bytes(b.begin() + 10, b.end() - 2);
}
Bytes trainer(bool realm, int level, int points = 100, const std::string &name = "Sword")
{
    Bytes b{1, uint8_t(points), uint8_t(realm), 0, 0, uint8_t(level), uint8_t(realm ? 3 : level + 1)};
    text(b, name);
    return b;
}
void playing(Client &c, int version)
{
    c.Start(0);
    feed(c, 0x22, {0, 0, 0, 0, 0});
    feed(c, 0x2a, {});
    Bytes roster(24 + (version >= 73 ? 10 : 8) * 184 + 104, 0);
    roster[24] = 'T';
    roster[24 + 124] = 14;
    feed(c, 0xfd, roster);
    feed(c, 0x28, {42, 0});
    feed(c, 0x2d, {0});
    feed(c, 0x20, {0, 7, 0, 50, 0, 0, 0, 10, 0, 0, 0, 20, 0, 0, 0, 0});
    feed(c, 0x2b, {0});
    check(c.GetState() == State::Playing, "join failed");
    Bytes npc(version >= 71 ? 28 : 24, 0);
    npc[1] = 9;
    npc[7] = 50;
    npc[11] = 20;
    npc[15] = 20;
    npc[20] = 50;
    npc[21] = 50;
    text(npc, "Trainer");
    text(npc, "");
    feed(c, 0xda, npc);
    check(c.SelectTarget(9, true), "select trainer");
    c.TakeOutgoing();
    // PacketLib168 VariousUpdate subtype 3 player-sheet level/name.
    Bytes sheet{3, 13, 0, 0, 50};
    text(sheet, "T");
    for (int i = 1; i < 13; ++i)
    {
        sheet.push_back(0);
        text(sheet, "");
    }
    feed(c, 0x16, sheet);
}
int main()
{
    try
    {
        for (int version : {68, 72, 73, 74})
        {
            Settings settings;
            settings.username = "account";
            settings.password = "secret";
            settings.character = "T";
            settings.version = uint8_t(version);
            Client c(settings);
            check(!c.Interact(9, 0), "offline interaction rejected");
            playing(c, version);
            feed(c, 0x7b, trainer(false, 1));
            check(!c.Trainer().open, "unsolicited window ignored");
            check(c.TrainingLevel() == 50, "i50 live player-sheet level used");
            check(c.Interact(9, 1), "nearby NPC interaction");
            auto out = c.TakeOutgoing();
            check(out.size() == 1 && out[0][9] == 0x7a && payload(out[0]) == Bytes({0, 0, 0, 10, 0, 0, 0, 20, 0, 42, 0, 9}), "classic interaction bytes");
            feed(c, 0x7b, trainer(false, 1));
            feed(c, 0x7b, trainer(true, 0, 10, "Determination"));
            check(c.Trainer().open && c.Trainer().specsKnown && c.Trainer().realmKnown, "both trainer pages parsed");
            // Full-width UpdatePoints must supersede wrapped trainer header points.
            feed(c, 0x91, {0, 0, 0, 0, 0, 0, 3, 232, 0, 0, 0, 0, 0, 10, 0, 0});
            check(c.MaxTrainingRank(c.Trainer().specs[0], false) == 44, "1000 point triangular budget");
            auto malformed = trainer(false, 1);
            malformed.pop_back();
            const auto revision = c.Trainer().revision;
            feed(c, 0x7b, malformed);
            check(c.Trainer().revision == revision && c.Trainer().specs[0].name == "Sword", "truncated list is atomic");
            auto duplicate = trainer(false, 1);
            duplicate[0] = 2;
            auto row = trainer(false, 1);
            duplicate.insert(duplicate.end(), row.begin() + 4, row.end());
            feed(c, 0x7b, duplicate);
            check(c.Trainer().revision == revision, "duplicate identifiers rejected");
            check(!c.Train(false, 0, 4, revision - 1, 1), "stale trainer view rejected");
            check(!c.Train(false, 0, 50, revision, 1), "overspend rejected");
            check(c.Train(false, 0, 4, revision, 1), "multi-rank allocation starts");
            out = c.TakeOutgoing();
            check(out.size() == 1 && out[0][9] == 0x7c && payload(out[0]) == Bytes({0, 0, 0, 10, 0, 0, 0, 20, 0, 0, 0, 0}), "classic training bytes");
            check(!c.Train(false, 0, 3, c.Trainer().revision, 1), "concurrent allocation rejected");
            feed(c, 0x7b, trainer(true, 0, 10, "Determination"));
            check(c.TakeOutgoing().empty(), "other page does not acknowledge spec");
            feed(c, 0x7b, trainer(false, 1));
            check(c.TakeOutgoing().empty(), "unchanged rank does not acknowledge spec");
            feed(c, 0x7b, trainer(false, 2));
            check(c.TakeOutgoing().size() == 1 && c.Trainer().pending, "rank two confirmation advances once");
            feed(c, 0x7b, trainer(false, 2));
            check(c.TakeOutgoing().empty(), "duplicate acknowledgement cannot double-send");
            feed(c, 0x7b, trainer(false, 3));
            check(c.TakeOutgoing().size() == 1, "rank three confirmation advances once");
            feed(c, 0x7b, trainer(false, 4));
            check(!c.Trainer().pending && c.TakeOutgoing().empty(), "allocation completes on confirmation");
            check(!c.Train(true, 0, 2, c.Trainer().revision, 1), "unknown future realm costs cannot be batched");
            check(c.Train(true, 0, 1, c.Trainer().revision, 1), "realm upgrade request");
            out = c.TakeOutgoing();
            check(payload(out[0]).back() == 100, "realm ability wire index offset");
            feed(c, 0x7b, trainer(true, 1, 7, "Determination"));
            check(!c.Trainer().pending, "realm upgrade confirmed");
            feed(c, 0x7b, trainer(true, 1, 7, "[Determination]"));
            check(!c.Train(true, 0, 2, c.Trainer().revision, 1), "unmet realm requirement disabled");
            check(c.Train(false, 0, 6, c.Trainer().revision, 2), "timeout fixture starts");
            c.TakeOutgoing();
            c.Tick(8);
            check(!c.Trainer().pending, "unacknowledged allocation times out");
            c.TakeOutgoing();
            feed(c, 0x7b, trainer(false, 5), 8);
            check(c.TakeOutgoing().empty(), "late confirmation cannot restart queue");
            check(!c.Train(false, 0, 6, c.Trainer().revision, 8), "timeout requires renewed interaction before purchase");
            check(c.Interact(9, 8), "refresh interaction after timeout");
            feed(c, 0x7b, trainer(false, 5), 8);
            c.TakeOutgoing();
            check(c.Train(false, 0, 6, c.Trainer().revision, 8), "new allocation after refreshed interaction");
            c.TakeOutgoing();
            c.CloseTrainer();
            feed(c, 0x7b, trainer(false, 6), 8);
            check(!c.Trainer().open && c.TakeOutgoing().empty(), "close cancels queue and ignores late window");
            check(c.Interact(9, 8), "reopen interaction");
            feed(c, 0x7b, trainer(false, 6), 8);
            c.SelectTarget(7, true);
            c.Tick(8);
            check(!c.Trainer().open, "changing target closes trainer");
            c.SelectTarget(9, true);
            check(c.Interact(9, 8), "reselect trainer");
            feed(c, 0x7b, trainer(false, 6), 8);
            c.Move(600, 20, 50, 0);
            c.Tick(8);
            check(!c.Trainer().open && !c.Interact(9, 8), "out of range closes trainer");
            c.Disconnect();
            check(!c.Trainer().open && c.Trainer().specs.empty() && !c.TrainingLevel(), "disconnect clears training state");
            Client other(settings);
            check(other.Trainer().revision != c.Trainer().revision, "session revisions cannot alias");
        }
        check(TrainingCost(1, 4, 2, false) == 9 && TrainingCost(4, 4, 5, false) == 0, "rank cost math");
        check(TrainingCost(0, 2, 3, true) == -1, "realm future upgrade costs unknown");
        std::cout << "Trainer protocol tests passed for 1.68, 1.72, 1.73 and 1.74\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
