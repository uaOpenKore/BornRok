#include "net/JobNames.hpp"

#include <unordered_map>

namespace uaro::net {

std::string jobName(u16 id) {
    // id -> display name. The transcendent 3rd jobs (_H, 4060+) and the mounted 3rd variants
    // (RUNE_KNIGHT2 etc.) intentionally share the base job's name, the way the client shows
    // them. Keep this in sync with roBrowser's DB/Jobs/JobConst.js ids.
    static const std::unordered_map<u16, const char*> kNames = {
        // Classic 1st / 2nd / special
        {0, "Novice"},       {1, "Swordman"},   {2, "Magician"},   {3, "Archer"},
        {4, "Acolyte"},      {5, "Merchant"},   {6, "Thief"},      {7, "Knight"},
        {8, "Priest"},       {9, "Wizard"},     {10, "Blacksmith"},{11, "Hunter"},
        {12, "Assassin"},    {13, "Knight"},    {14, "Crusader"},  {15, "Monk"},
        {16, "Sage"},        {17, "Rogue"},     {18, "Alchemist"}, {19, "Bard"},
        {20, "Dancer"},      {21, "Crusader"},  {22, "Wedding"},   {23, "Super Novice"},
        {24, "Gunslinger"},  {25, "Ninja"},     {26, "Christmas"}, {27, "Summer"},
        // Transcendent 1st / 2nd (4001+)
        {4001, "High Novice"},   {4002, "High Swordman"}, {4003, "High Magician"},
        {4004, "High Archer"},   {4005, "High Acolyte"},  {4006, "High Merchant"},
        {4007, "High Thief"},    {4008, "Lord Knight"},   {4009, "High Priest"},
        {4010, "High Wizard"},   {4011, "Whitesmith"},    {4012, "Sniper"},
        {4013, "Assassin Cross"},{4014, "Lord Knight"},   {4015, "Paladin"},
        {4016, "Champion"},      {4017, "Professor"},     {4018, "Stalker"},
        {4019, "Creator"},       {4020, "Clown"},         {4021, "Gypsy"},
        {4022, "Paladin"},
        // Baby (4023+)
        {4023, "Baby Novice"},   {4024, "Baby Swordman"}, {4025, "Baby Magician"},
        {4026, "Baby Archer"},   {4027, "Baby Acolyte"},  {4028, "Baby Merchant"},
        {4029, "Baby Thief"},    {4030, "Baby Knight"},   {4031, "Baby Priest"},
        {4032, "Baby Wizard"},   {4033, "Baby Blacksmith"},{4034, "Baby Hunter"},
        {4035, "Baby Assassin"}, {4036, "Baby Knight"},   {4037, "Baby Crusader"},
        {4038, "Baby Monk"},     {4039, "Baby Sage"},     {4040, "Baby Rogue"},
        {4041, "Baby Alchemist"},{4042, "Baby Bard"},     {4043, "Baby Dancer"},
        {4044, "Baby Crusader"}, {4045, "Super Baby"},
        // Expanded
        {4046, "Taekwon"},       {4047, "Star Gladiator"},{4048, "Star Gladiator"},
        {4049, "Soul Linker"},
        // Renewal 3rd jobs (regular 4054+ and transcendent _H 4060+ share a display name)
        {4054, "Rune Knight"},   {4055, "Warlock"},       {4056, "Ranger"},
        {4057, "Arch Bishop"},   {4058, "Mechanic"},      {4059, "Guillotine Cross"},
        {4060, "Rune Knight"},   {4061, "Warlock"},       {4062, "Ranger"},
        {4063, "Arch Bishop"},   {4064, "Mechanic"},      {4065, "Guillotine Cross"},
        {4066, "Royal Guard"},   {4067, "Sorcerer"},      {4068, "Minstrel"},
        {4069, "Wanderer"},      {4070, "Sura"},          {4071, "Genetic"},
        {4072, "Shadow Chaser"}, {4073, "Royal Guard"},   {4074, "Sorcerer"},
        {4075, "Minstrel"},      {4076, "Wanderer"},      {4077, "Sura"},
        {4078, "Genetic"},       {4079, "Shadow Chaser"},
        // Mounted 3rd variants -> base name
        {4080, "Rune Knight"},   {4081, "Rune Knight"},   {4082, "Royal Guard"},
        {4083, "Royal Guard"},   {4084, "Ranger"},        {4085, "Ranger"},
        {4086, "Mechanic"},      {4087, "Mechanic"},
        // Baby 3rd
        {4096, "Baby Rune Knight"},  {4097, "Baby Warlock"},   {4098, "Baby Ranger"},
        {4099, "Baby Arch Bishop"},  {4100, "Baby Mechanic"},  {4101, "Baby Guillotine Cross"},
        {4102, "Baby Royal Guard"},  {4103, "Baby Sorcerer"},  {4104, "Baby Minstrel"},
        {4105, "Baby Wanderer"},     {4106, "Baby Sura"},      {4107, "Baby Genetic"},
        {4108, "Baby Shadow Chaser"},
    };
    if (auto it = kNames.find(id); it != kNames.end()) return it->second;
    return "Job " + std::to_string(id);
}

}  // namespace uaro::net
