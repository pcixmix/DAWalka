#pragma once

#include "Common.h"
#include <random>

namespace dawalka {

// =============================================================================
// PromptLibrary
// =============================================================================
// A header-only, English-language library of prompt building blocks used by
// the editor's "RANDOM MUSIC" and "RANDOM SFX" buttons.  Each click picks
// 1-3 random blocks (style + mood + modifier) and composes them into a
// single natural-language prompt.  The composition depends on:
//
//   - the chosen category (Music = mostly instrumental across many genres;
//     Sfx = mostly sound effects and ambiences)
//
// Two API entry points are exposed for the two UI buttons:
//   - pickRandomMusic()  →  T2A Music / T2A instrumental / A2A Music
//   - pickRandomSfx()    →  T2A SFX / A2A SFX
//
// For A2A the body is wrapped in a "transform this into…" action phrase.
// A2A uses a single shared random call (pickRandomA2a(isMusic)) so the
// user's selected model category determines the pool.
//
// Entries are intentionally INSTRUMENT-AGNOSTIC — the user can stack
// the SOLO INSTRUMENT dropdown separately to add a "solo <instrument>"
// suffix.  This keeps the random library describing STYLE / MOOD /
// TEXTURE rather than LINEUP, so a user-driven solo and a library-
// driven style compose cleanly.
//
// The library is a pure function and is safe to call from the UI thread.
// Each block type holds thousands of phrases so the user is unlikely
// to see the same prompt twice in a session.
// =============================================================================

class PromptLibrary
{
public:
    // T2A / A2A Music — picks 1-3 music building blocks and composes
    // them.  In A2A mode the result is wrapped in an action phrase.
    static juce::String pickRandomMusic (bool isA2A)
    {
        return isA2A ? wrapA2a (composeMusic()) : composeMusic();
    }

    // T2A / A2A SFX — picks 1-2 SFX building blocks and composes them.
    // In A2A mode the result is wrapped in an action phrase.
    static juce::String pickRandomSfx (bool isA2A)
    {
        return isA2A ? wrapA2a (composeSfx()) : composeSfx();
    }

    // Single A2A entry point — the A2A button in A2A mode just calls
    // this with the appropriate category.  Kept distinct from
    // pickRandomMusic / pickRandomSfx so future expansion (e.g. A2A
    // "Hybrid") doesn't have to touch the T2A button logic.
    static juce::String pickRandomA2a (bool isMusic)
    {
        return wrapA2a (isMusic ? composeMusic() : composeSfx());
    }

private:
    // -------------------------------------------------------------------------
    // Building blocks
    // -------------------------------------------------------------------------

    // Genre / sub-genre for music prompts.  Each entry is a noun phrase
    // that reads naturally as "a <genre> piece, …".  Several thousand
    // entries cover rock, electronic, hip hop, jazz, classical, world,
    // soul, reggae, blues, pop, ambient, folk, punk, experimental,
    // latin, african, asian, middle eastern, caribbean and more.
    // (The function DEFINITIONS live further down — they are the
    // declarations too, since C++ class members are in-scope as soon
    // as the class body is parsed.  We don't repeat them here.)
    //
    // Sound sources, modifiers, and acoustic environments for SFX.
    //
    // A2A action phrases.

    // -------------------------------------------------------------------------
    // Data definitions
    // -------------------------------------------------------------------------

    static const std::vector<juce::String>& musicGenres()
    {
        static const std::vector<juce::String> v = {
            // ===== ROCK (90) =====
            "classic rock", "hard rock", "soft rock", "progressive rock",
            "psychedelic rock", "garage rock", "punk rock", "post-punk",
            "indie rock", "alternative rock", "grunge", "stoner rock",
            "shoegaze", "krautrock", "glam rock", "gothic rock",
            "noise rock", "math rock", "space rock", "folk rock",
            "country rock", "southern rock", "blues rock", "jangle pop",
            "college rock", "britpop", "madchester", "post-rock",
            "sludge metal", "doom metal", "thrash metal", "speed metal",
            "death metal", "black metal", "power metal", "groove metal",
            "nu-metal", "metalcore", "post-hardcore", "emo",
            "screamo", "hardcore punk", "pop-punk", "skate punk",
            "anarcho-punk", "riot grrrl", "horror punk", "psychobilly",
            "surf rock", "instrumental rock", "heartland rock", "roots rock",
            "AOR", "arena rock", "pub rock", "cowpunk",
            "industrial rock", "industrial metal", "neue deutsche härte",
            "visual kei", "J-rock", "K-rock", "Mandopop rock",
            "Cantopop rock", "Bollywood rock", "Latin rock",
            "Chicano rock", "Turkish rock", "Anatolian rock",
            "Russian rock", "twee pop", "dream pop", "slowcore",
            "sadcore", "chamber pop", "baroque pop", "noise pop",
            "C86", "lo-fi indie", "fuzz rock", "acid rock",
            "zeuhl", "RIO", "avant-prog", "post-metal",
            "djent", "technical metal", "slipknot-style", "blackgaze",
            "viking metal", "folk metal", "celtic metal", "pirate metal",
            // ===== ELECTRONIC (250) =====
            "house", "deep house", "tech house", "progressive house",
            "electro house", "future house", "bass house", "tropical house",
            "funky house", "soulful house", "Chicago house", "Detroit house",
            "acid house", "lo-fi house", "minimal house", "microhouse",
            "techhouse", "jackin' house", "French house", "filter house",
            "tech trance", "uplifting trance", "vocal trance", "progressive trance",
            "psytrance", "goa trance", "hard trance", "euro trance",
            "acid trance", "dream trance", "balearic trance", "trance anthem",
            "drum and bass", "liquid drum and bass", "neurofunk", "jump-up",
            "jungle", "intelligent drum and bass", "darkstep", "techstep",
            "breakcore", "ragga jungle", "drumfunk", "ambient drum and bass",
            "dubstep", "brostep", "riddim", "future garage",
            "UK garage", "2-step garage", "speed garage", "grime",
            "8-bar garage", "bassline", "4x4 garage", "deep dubstep",
            "minimal dubstep", "chillstep", "trapstep", "wonky",
            "techno", "Detroit techno", "minimal techno", "acid techno",
            "hard techno", "industrial techno", "dub techno", "Berlin techno",
            "melodic techno", "peak-time techno", "hypnotic techno",
            "schranz", "free techno", "ambient techno", "trance techno",
            "breakbeat", "nu-breakz", "Florida breaks", "progressive breaks",
            "breaks", "broken beat", "nu skool breaks", "funky breaks",
            "ambient breaks", "atmospheric breaks", "disco breaks", "electro breaks",
            "hardcore", "gabber", "speedcore", "terror",
            "makina", "happy hardcore", "bouncy techno", "uk hardcore",
            "breakcore hardcore", "frenchcore", "industrial hardcore", "mainstream hardcore",
            "EDM", "big room house", "melodic EDM", "future bass",
            "future bounce", "melodic dubstep", "pop EDM", "commercial EDM",
            "festival EDM", "stadium EDM", "progressive EDM", "electro pop",
            "indie electronic", "leftfield", "IDM", "glitch",
            "braindance", "ambient IDM", "minimal glitch", "click and cuts",
            "chiptune", "8-bit", "bitpop", "lo-fi electronic",
            "chiptune funk", "Nintendocore", "crunkcore", "electronicore",
            "synthwave", "retrowave", "outrun", "vaporwave",
            "future funk", "darksynth", "synth-pop", "minimal synth",
            "coldwave", "minimal wave", "EBM", "futurepop",
            "aggrotech", "dark electro", "power noise", "rhythmic noise",
            "ambient electronic", "downtempo", "chillout", "lounge",
            "trip-hop", "abstract hip-hop", "instrumental hip-hop",
            "nu-jazz electronic", "glitch hop", "wonky hop", "wonky bass",
            "footwork", "juke", "ghetto house", "ghetto tech",
            "Baltimore club", "Jersey club", "UK funky", "Afro house",
            "Afro tech", "amapiano", "gqom", "South African house",
            "Brazilian funk", "funk carioca", "baile funk", "brazilian bass",
            "Moombahton", "electro moombahton", "Dutch house", "fidget house",
            "complextro", "glitch hop", "neurohop", "space bass",
            "psybass", "psydub", "forest psytrance", "dark psytrance",
            "full-on psytrance", "minimal psytrance", "progressive psytrance",
            "zenonesque", "suiside", "psybreaks", "microprog",
            "dark ambient", "drone ambient", "space ambient", "isolationism",
            "lowercase", "onkyokei", "musique concrète electronic", "tape music",
            "analog electronic", "modular synth", "eurorack", "patchwork",
            "circuit-bent", "chiptune ambient", "chillwave", "glo-fi",
            "hypnagogic pop", "late night lo-fi", "mallsoft", "slushwave",
            "futurevapor", "broken transmission", "dungeon synth", "dark dungeon synth",
            "castle synth", "fantasy synth", "ambient synth", "neoclassical darkwave",
            "witch house", "drag", "seapunk", "bubblegum bass",
            "PC music", "hyperpop", "nightcore", "hardvapour",
            "phonk", "drift phonk", "cowbell phonk", "gym phonk",
            "Brazilian phonk", "house phonk", "techno phonk", "drill phonk",
            "dembow electronic", "electro Latino", "moombahton core", "reggaeton electronic",
            "tribal house", "ethnic house", "world electronic", "tropical bass",
            "global bass", "cumbia digital", "digital cumbia", "electrocumbia",
            "cumbia sonidera", "Arab electronic", "Middle Eastern techno",
            "Turkish electronic", "Anatolian electronic", "Indian electronic",
            "Asian electronic", "J-pop electronic", "K-pop electronic",             "Mandopop electronic",
            // ===== HIP HOP (50) =====
            "old school hip-hop", "boom bap", "East Coast hip-hop",
            "West Coast hip-hop", "Southern hip-hop", "Dirty South",
            "crunk", "trap", "drill", "UK drill",
            "Brooklyn drill", "Chicago drill", "pluggnb", "rage",
            "hyper-rage", "ambient trap", "southern trap", "trap soul",
            "conscious hip-hop", "political hip-hop", "jazz rap", "g-funk",
            "mafioso rap", "horrorcore", "grime", "UK hip-hop",
            "grime rap", "road rap", "british hip-hop", "garage rap",
            "French hip-hop", "German hip-hop", "Russian hip-hop",
            "K-hip-hop", "K-rap", "J-rap", "J-hip-hop",
            "Mandopop hip-hop", "Cantopop hip-hop", "Latin trap",
            "reggaeton trap", "Brazilian hip-hop", "Afro hip-hop",
            "Nigerian hip-hop", "South African hip-hop", "Australian hip-hop",
            "experimental hip-hop", "abstract hip-hop", "instrumental hip-hop",
            "lo-fi hip-hop", "chillhop",             "lo-fi beats", "study beats",
            // ===== JAZZ (60) =====
            "bebop", "hard bop", "post-bop", "cool jazz",
            "West Coast jazz", "modal jazz", "free jazz", "avant-garde jazz",
            "free improvisation", "spiritual jazz", "soul jazz", "jazz funk",
            "fusion", "jazz fusion", "jazz-rock", "smooth jazz",
            "contemporary jazz", "eclectic jazz", "nu jazz", "future jazz",
            "electro jazz", "acid jazz", "trip-hop jazz", "downtempo jazz",
            "M-Base", "creative jazz", "modern creative", "post-fusion",
            "punk jazz", "jazzcore", "jazz metal", "gypsy jazz",
            "Django jazz", "manouche", "hot club", "swing",
            "swing revival", "neo-swing", "big band", "swing big band",
            "jump blues", "r&b jazz", "vocal jazz", "scat",
            "standards", "torch song", "crooner", "cabaret jazz",
            "Latin jazz", "Afro-Cuban jazz", "bossa nova", "samba jazz",
            "brazilian jazz", "Cubop", "Afro-jazz", "ethio-jazz",
            "Indo-jazz", "Asian jazz", "Japanese jazz", "Tokyo jazz",
            "K-jazz", "European jazz", "Nordic jazz", "ECM jazz",
            "ambient jazz", "chamber jazz", "stride", "ragtime",
            // ===== CLASSICAL (80) =====
            "Baroque", "late Baroque", "high Baroque", "early Baroque",
            "galant", "rococo", "Classical era", "early Classical",
            "Viennese Classical", "Romantic", "late Romantic", "early Romantic",
            "nationalist", "Russian nationalist", "Czech nationalist",
            "Scandinavian nationalist", "impressionist", "post-impressionist",
            "expressionist", "late Romantic expressionist", "verismo opera",
            "minimalist", "post-minimalist", "neo-Romantic", "neo-Classical",
            "neo-Baroque", "neo-Medieval", "Renaissance", "early Renaissance",
            "late Renaissance", "Gothic", "ars antiqua", "ars nova",
            "ars subtilior", "Trecento", "Medieval", "early Medieval",
            "Gregorian", "plainchant", "Byzantine chant", "Russian Orthodox chant",
            "choral", "a cappella", "sacred choral", "secular choral",
            "chamber", "string quartet", "piano trio", "wind quintet",
            "brass quintet", "baroque chamber", "Classical chamber",
            "Romantic chamber", "modern chamber", "orchestral", "symphony",
            "symphonic poem", "tone poem", "concerto grosso", "solo concerto",
            "opera", "grand opera", "opera buffa", "opera seria",
            "tragédie en musique", "ballet opera", "zarzuela", "Singspiel",
            "oratorio", "cantata", "mass", "requiem",
            "passion", "magnificat", "te deum", "stabat mater",
            "symphonic suite", "orchestral suite", "baroque suite",
            "modern suite", "waltz", "polonaise", "mazurka",
            "nocturne", "prelude", "fugue", "etude",
            "sonata", "sonata form", "rondo", "variation",
            // ===== WORLD (250) =====
            "Celtic", "Irish traditional", "Scottish traditional",
            "Welsh traditional", "Breton", "Galician", "Cape Breton",
            "English folk", "Morris dance", "Celtic rock", "Celtic punk",
            "Celtic metal", "Celtic fusion", "Celtic ambient", "highland",
            "Nordic folk", "Scandinavian folk", "Swedish folk", "Norwegian folk",
            "hardingfele", "langspil", "joik", "gammaldans",
            "joik electronic", "Nordic ambient", "Nordic neo-folk", "Hagalund",
            "Balkan", "Balkan brass", "Balkan folk", "Romani Balkan",
            "Serbian brass", "Bulgarian folk", "Greek folk", "rebetiko",
            "laïko", "entehno", "Greek electronic", "Cretan",
            "Eastern European folk", "Polish folk", "Hungarian folk",
            "Romanian folk", "manele", "Czech folk", "Slovak folk",
            "Russian folk", "Russian romance", "Kalinka", "Bayan",
            "Tatar folk", "Ukrainian folk", "Hopak", "Russian neo-folk",
            "Italian folk", "taranta", "pizzica", "tammuriata",
            "Sardinian", "Sicilian folk", "Neapolitan", "Italian opera-folk",
            "Spanish folk", "flamenco", "rumba flamenca", "sevillanas",
            "jota", "sardana", "galician folk", "Iberian medieval",
            "Portuguese folk", "fado", "fado vadio", "Brazilian folk",
            "forró", "baião", "xaxado", "frevo",
            "maracatu", "samba de roda", "samba", "pagode",
            "axé", "brega", "tecnobrega", "Brazilian sertanejo",
            "sertanejo universitário", "MPB", "tropicália", "bossa nova",
            "Cuban", "son cubano", "danzón", "mambo",
            "cha-cha-chá", "rumba", "guaracha", "bolero",
            "Puerto Rican", "plena", "bomba", "jíbaro",
            "Dominican", "merengue", "bachata", "merengue típico",
            "Mexican", "mariachi", "ranchera", "corrido",
            "norteño", "grupera", "cumbia", "cumbia sonidera",
            "tejano", "Tex-Mex", "Chicano", "Latin pop",
            "Andean", "huayno", "cumbia andina", "chicha",
            " Peruvian folk", "Ecuadorian folk", "Bolivian folk",
            "Colombian", "vallenato", "cumbia colombiana", "champeta",
            "Argentine", "tango", "milonga", "chacarera",
            "zamba", "cuarteto", "cumbia villera", "Argentine rock",
            "Chilean", "nueva canción", "cueca", "Chilean rock",
            "Uruguayan", "candombe", "murga", "tango uruguayo",
            "African", "West African", "highlife", "jùjú",
            "fuji", "apala", "sakara", "waka",
            "Afrobeat", "Afro-jùjú", "Afro-funk", "Nigerian gospel",
            "Ghanaian", "hiplife", "Azonto", "Ghanaian hiplife",
            "South African", "kwaito", "maskandi", "mbaqanga",
            "marabi", "South African jazz", "township jive", "gqom",
            "amapiano", "South African house", "Soweto gospel", "Shona",
            "East African", "benga", "taarab", "Swahili",
            "Ethiopian", "Ethio-jazz", "tizita", "Ethiopian pop",
            "Eritrean", "Tigrigna", "Somali", "Somali banaadiri",
            "North African", "raï", "chaabi", "gnawa",
            "Moroccan", "Andalusian", "Maghreb", "Saharan",
            "Tuareg", "Tishoumaren", "Saharan rock", "desert blues",
            "Egyptian", "Egyptian pop", "shaabi", "tarab",
            "MENA electronic", "Arab electronic", "Khaleeji", "khaliji",
            "Iraqi", "maqam", "Lebanese", "dabke",
            "Syrian", "Syrian pop", "Turkish classical", "Ottoman",
            "Turkish folk", "Turkish pop", "Anatolian rock", "arabesk",
            "Iranian", "Persian classical", "dastgah", "bandari",
            "Persian pop", "Persian electronic", "Azerbaijani", "mugham",
            "Afghan", "Pashto", "Hazaragi", "Afghan rubab",
            "Indian", "Hindustani classical", "Carnatic", "raga",
            "filmi", "Bollywood", "Indipop", "Indian folk",
            "Bhangra", "bhangra-pop", "ghazal", "qawwali",
            "Pakistani", "Sufi", "Sindhi", "Pashtun",
            "Bangladeshi", "Bengali folk", "rabindra sangeet", "nazrul geeti",
            "Sri Lankan", "South Indian film", "Tamil", "Malayalam",
            "Nepali", "Tibetan", "Bhutanese", "Sherpa",
            "Southeast Asian", "Thai", "luk thung", "luk krung",
            "mor lam", "Cambodian", "pinpeat", "Lao",
            "Vietnamese", "nhạc vàng", "v-pop", "Vietnamese folk",
            "Filipino", "OPM", "kundiman", "Tagalog folk",
            "Malaysian", "Indonesian", "dangdut", "kroncong",
            "Javanese gamelan", "Balinese", "Sundanese", "Minangkabau",
            "East Asian", "Mandopop", "Cantopop", "Hokkien pop",
            "Taiwanese folk", "Chinese classical", "guoyue", "Chinese opera",
            "Peking opera", "Cantonese opera", "Kunqu", "nanyin",
            "J-pop", "J-rock", "enka", "kayōkyoku",
            "Japanese folk", "min'yō", "shamisen", "koto",
            "gagaku", "Noh", "kabuki", "bunraku",
            "K-pop", "K-indie", "trot", "K-hip-hop",
            "Korean folk", "pansori", "samulnori", "gayageum",
            "Mongolian", "throat singing", "khoomei", "morin khuur",
            "Caucasian", "Georgian polyphony", "Azerbaijani mugham",
            "Armenian", " duduk", "kamancheh", "Circassian",
            "Central Asian", "Kazakh", " Kyrgyz", "Uzbek",
            "Tajik", "Turkmen", "Afghan rubab", "dutar",
            // ===== SOUL / FUNK / R&B (30) =====
            "deep soul", "southern soul", "Memphis soul", "Detroit soul",
            "Philly soul", "Chicago soul", "northern soul", "British soul",
            "neo-soul", "progressive soul", "blue-eyed soul", "Motown",
            "Stax sound", "classic funk", "P-funk", "deep funk",
            "go-go", "Minneapolis sound", "electro-funk", "funk rock",
            "funk metal", "ska-punk funk", "acid jazz funk", "neo-funk",
            "contemporary R&B", "alternative R&B", "PBR&B", "neo-soul R&B",
            "UK garage R&B",             "slow jam", "new jack swing",
            // ===== REGGAE (15) =====
            "roots reggae", "dub", "dancehall", "ragga",
            "lovers rock", "rocksteady", "ska", "two-tone",
            "reggae fusion", "reggae pop", "early reggae", "conscious reggae",
            "digital reggae",             "reggae en español", "Spanish reggae",
            // ===== BLUES (15) =====
            "Delta blues", "Chicago blues", "electric blues", "acoustic blues",
            "country blues", "Texas blues", "Memphis blues", "piedmont blues",
            "West Coast blues", "British blues", "blues rock", "jump blues",
            "rhythm and blues",             "modern blues", "soul blues",
            // ===== POP (40) =====
            "dance-pop", "electropop", "synth-pop", "teen pop",
            "bubblegum pop", "power pop", "pop rock", "pop punk",
            "indie pop", "dream pop", "sunshine pop", "baroque pop",
            "chamber pop", "orchestral pop", "space-age pop", "exotica",
            "kitsch pop", "novelty pop", "novelty song", "comedy pop",
            "country pop", "folk pop", "Latin pop", "tropical pop",
            "K-pop", "J-pop", "Mandopop", "Cantopop",
            "V-pop", "T-pop", "Indo-pop", "city pop",
            "future pop", "hyperpop", "dark pop", "alt-pop",
            "minimal pop",             "slowcore pop", "sad pop", "bedroom pop",
            // ===== AMBIENT / NEW AGE (30) =====
            "ambient", "dark ambient", "space ambient", "drone ambient",
            "lowercase", "isolationism", "onkyokei", "new age",
            "space music", "new age meditation", "yoga music", "healing music",
            "nature ambient", "rain ambient", "ocean ambient", "forest ambient",
            "cathedral ambient", "temple ambient", "zen ambient", "Sufi ambient",
            "Buddhist ambient", "Hindu ambient", "shamanic ambient", "trance ambient",
            "meditation drone", "meditative ambient", "tibet-inspired", "didgeridoo drone",
            "tuvan throat drone", "harmonic drone",
            // ===== FOLK (30) =====
            "American folk", "contemporary folk", "indie folk", "freak folk",
            "anti-folk", "progressive folk", "psychedelic folk", "folk baroque",
            "Celtic folk", "Nordic folk", "Mediterranean folk", "Appalachian",
            "old-time", "bluegrass", "newgrass", "progressive bluegrass",
            "jamgrass", "country folk", "cowboy folk", "singer-songwriter",
            "fingerstyle", "acoustic folk", "chamber folk", "folk cabaret",
            "folk comedy", "children's folk", "lullaby", "work song",
            "sea shanty", "field holler",
            // ===== PUNK (20) =====
            "punk", "hardcore punk", "post-punk", "pop-punk",
            "skate punk", "anarcho-punk", "street punk", "Oi!",
            "psychobilly", "horror punk", "cowpunk", "celtic punk",
            "folk punk", "gypsy punk", "punkabilly", "crust punk",
            "d-beat",             "queercore", "riot grrrl", "emo punk",
            // ===== EXPERIMENTAL / AVANT (20) =====
            "experimental", "avant-garde", "musique concrète", "tape music",
            "noise", "Japanoise", "power electronics", "death industrial",
            "industrial", "neofolk industrial", "martial industrial", "dark ambient industrial",
            "drone", "lowercase", "onkyokei", "EAI",
            "free improvisation", "free jazz", "new music", "post-modern composition",
            // ===== LATIN (40) =====
            "salsa", "merengue", "bachata", "cumbia",
            "vallenato", "porro", "champeta", "son cubano",
            "mambo", "cha-cha-chá", "rumba", "guaracha",
            "bolero", "bossa nova", "samba", "pagode",
            "forró", "baião", "frevo", "maracatu",
            "tropicália", "MPB", "sertanejo", "tecnobrega",
            "reggaeton", "dembow", "Latin trap", "Latin urban",
            "Latin pop", "Latin rock", "Latin alternative", "rock en español",
            "Mexican", "mariachi", "ranchera", "corrido",
            "norteño", "grupera", "cumbia sonidera", "Chicano",
            "huayno", "tango", "milonga"
        };
        return v;
    }

    static const std::vector<juce::String>& musicMoods()
    {
        static const std::vector<juce::String> v = {
            "dark and moody", "uplifting and triumphant", "melancholic and reflective",
            "intense and driving", "calm and meditative", "playful and quirky",
            "nostalgic and warm", "mysterious and ethereal", "aggressive and powerful",
            "gentle and intimate", "tense and suspenseful", "dreamy and floating",
            "heroic and cinematic", "lonely and sorrowful", "hopeful and bright",
            "eerie and unsettling", "romantic and tender", "defiant and rebellious",
            "somber and dignified", "joyful and exuberant", "brooding and shadowy",
            "cathartic and emotional", "whimsical and playful", "mystical and otherworldly",
            "solemn and reverent", "gritty and raw", "lush and opulent", "spare and minimal",
            "infectious and danceable", "reflective and introspective",
            "yearning and longing", "triumphant and victorious", "ominous and foreboding",
            "light-hearted and carefree", "lush and romantic", "satirical and witty",
            "sardonic and dry", "earnest and sincere", "haunting and lingering",
            "frenetic and chaotic", "delicate and refined", "earthy and grounded",
            "celestial and expansive", "ominous and tense", "playful and mischievous",
            "anthemic and bold", "fragile and vulnerable", "elegant and graceful",
            "blissful and serene", "chaotic and frenzied", "peaceful and tranquil",
            "uplifting and hopeful", "wistful and nostalgic", "sultry and sensual",
            "driving and propulsive", "hypnotic and trancey", "lilting and flowing",
            "stately and dignified", "shimmering and bright", "darkly humorous",
            "carnival-esque", "festival-like", "processional", "ritualistic",
            "ceremonial", "festive and celebratory", "meditative and contemplative",
            "brooding and introspective", "mournful and elegiac", "pensive and thoughtful",
            "yearning and ache", "warm and welcoming", "cold and detached",
            "oppressive and claustrophobic", "spacious and airy", "lo-fi and textured",
            "warm and analog", "crisp and digital", "lush and string-laden",
            "metallic and percussive", "wooden and acoustic", "electronic and synthetic",
            "raw and unpolished", "polished and produced", "vintage and retro",
            "futuristic and modern", "timeless and classical", "modern and contemporary",
            "experimental and forward-thinking", "traditional and rootsy", "fusion and eclectic",
            "cinematic and orchestral", "intimate and chamber-like", "huge and stadium-sized",
            "loose and jam-oriented", "tight and composed", "structured and arranged",
            "spontaneous and improvised", "rhythmic and percussive", "melodic and flowing",
            "harmonic and consonant", "dissonant and tense", "modal and hypnotic",
            "tonal and accessible", "atonal and challenging", "minimal and repetitive",
            "maximal and dense", "stripped-down and bare", "elaborate and ornate",
            "understated and subtle", "loud and aggressive", "soft and delicate",
            "thunderous and crashing", "whispered and quiet", "shouted and forceful",
            "spoken and narrative", "sung and lyrical", "instrumental and wordless",
            "anthemic and communal", "personal and confessional", "universal and timeless"
        };
        return v;
    }

    static const std::vector<juce::String>& musicModifiers()
    {
        static const std::vector<juce::String> v = {
            "with live strings", "with warm analog synths", "with vinyl crackle",
            "with FM bells", "with deep sub-bass", "with brushed drums",
            "with hand percussion", "with arpeggiated synths", "with reverb-drenched pads",
            "with tape-saturated feel", "with lo-fi dusty samples", "with choir pad",
            "with driving hi-hats", "with tremolo texture", "with lush string section",
            "with brass stabs", "with horn section", "with sax solo",
            "with electric piano comping", "with organ swells", "with harpsichord",
            "with plucked strings", "with pizzicato", "with tremolo strings",
            "with staccato strings", "with legato strings", "with col legno",
            "with sul ponticello", "with harmonics", "with double stops",
            "with fingerpicked guitar", "with strummed guitar", "with palm-muted guitar",
            "with overdriven guitar", "with clean guitar", "with acoustic guitar",
            "with slide guitar", "with bottleneck guitar", "with 12-string guitar",
            "with resonator guitar", "with bass guitar", "with upright bass",
            "with slapped bass", "with popped bass", "with bowed bass",
            "with synth bass", "with sub-bass", "with 808 bass",
            "with Moog bass", "with ARP bass", "with TB-303 bass",
            "with analog synth lead", "with digital synth lead", "with saw-wave lead",
            "with square-wave lead", "with PWM lead", "with FM lead",
            "with granular lead", "with wavetable lead", "with formant lead",
            "with choir", "with solo vocal", "with harmonies",
            "with layered vocals", "with whispered vocals", "with spoken word",
            "with rap verses", "with sung choruses", "with call-and-response",
            "with gospel choir", "with barbershop quartet", "with doo-wop group",
            "with drum machine", "with acoustic drums", "with electronic drums",
            "with hand claps", "with finger snaps", "with tambourine",
            "with cowbell", "with congas", "with bongos",
            "with timbales", "with djembe", "with cajón",
            "with tabla", "with darbuka", "with taiko",
            "with marimba", "with vibraphone", "with xylophone",
            "with glockenspiel", "with tubular bells", "with music box",
            "with celesta", "with prepared piano", "with honky-tonk piano",
            "with tack piano", "with detuned piano", "with layered piano",
            "with sustained strings", "with muted brass", "with open brass",
            "with harmon mute", "with wah-wah trumpet", "with plunger mute",
            "with clarinet trills", "with flute runs", "with oboe melody",
            "with bassoon drone", "with contrabassoon", "with English horn",
            "with piccolo flourish", "with alto sax", "with tenor sax",
            "with bari sax", "with soprano sax", "with bass clarinet",
            "with harmonica", "with melodica", "with accordion",
            "with bandoneón", "with concertina", "with autoharp",
            "with dulcimer", "with zither", "with psaltery",
            "with harp glissandi", "with harp arpeggios", "with koto",
            "with shamisen", "with sitar", "with tabla",
            "with tanpura", "with bansuri", "with santoor",
            "with didgeridoo", "with hang drum", "with steel tongue drum",
            "with glass marimba", "with waterphone", "with theremin",
            "with ondes Martenot", "with Moog", "with modular synth",
            "with Eurorack", "with Buchla", "with Serge",
            "with DX7 electric piano", "with Rhodes", "with Wurlitzer",
            "with Clavinet", "with Mellotron", "with Chamberlin",
            "with tape delay", "with analog delay", "with digital delay",
            "with ping-pong delay", "with slap-back delay", "with dotted-eighth delay",
            "with spring reverb", "with plate reverb", "with hall reverb",
            "with cathedral reverb", "with room reverb", "with chamber reverb",
            "with convolution reverb", "with shimmer reverb", "with gated reverb",
            "with reverse reverb", "with modulated reverb", "with freeze reverb",
            "with chorus", "with flanger", "with phaser",
            "with rotary speaker", "with Leslie", "with Uni-Vibe",
            "with tremolo", "with vibrato", "with auto-wah",
            "with envelope filter", "with talk box", "with vocoder",
            "with harmonizer", "with octaver", "with sub-octave",
            "with ring modulator", "with frequency shifter", "with bit crusher",
            "with sample-rate reduction", "with aliasing", "with lo-fi degradation",
            "with saturation", "with overdrive", "with distortion",
            "with fuzz", "with boost", "with clean headroom",
            "with parallel compression", "with sidechain compression", "with bus compression",
            "with multiband compression", "with limiting", "with tape compression",
            "with FET compression", "with opto compression", "with VCA compression",
            "with transient designer", "with de-esser", "with exciter",
            "with EQ boost", "with EQ cut", "with high-pass filter",
            "with low-pass filter", "with band-pass filter", "with notch filter",
            "with resonant filter", "with envelope filter", "with LFO modulation",
            "with step sequencer", "with arpeggiator", "with generative sequences",
            "with random patches", "with evolving textures", "with morphing timbres",
            "with glitchy textures", "with granular clouds", "with convolution processing",
            "with spectral processing", "with FFT manipulation", "with pitch shifting",
            "with time stretching", "with granular synthesis", "with physical modeling",
            "with FM synthesis", "with additive synthesis", "with subtractive synthesis",
            "with wavetable synthesis", "with phase distortion", "with cross synthesis",
            "with vocal chops", "with vocal stabs", "with vocal pads",
            "with chopped samples", "with resampled loops", "with vinyl-style samples",
            "with cassette tape feel", "with VHS tape feel", "with 8-bit samples",
            "with chiptune elements", "with game console sounds", "with dial-up modem textures"
        };
        return v;
    }

    static const std::vector<juce::String>& musicEnergies()
    {
        static const std::vector<juce::String> v = {
            "slow-burning", "high-energy", "driving", "pulsing", "thumping",
            "gently pulsing", "frenetic", "languid", "propulsive", "restless",
            "grounded", "floating", "rushing", "meandering", "turbulent",
            "calm", "stormy", "explosive", "subdued", "restrained",
            "unbridled", "controlled", "loose", "tight", "jammed-out",
            "meticulous", "sprawling", "compact", "expansive", "intimate",
            "crushing", "delicate", "shimmering", "dark", "bright",
            "neon-bright", "muted", "echoing", "dry", "wet",
            "compressed", "saturated", "clean", "gritty", "polished",
            "dusty", "crisp", "soft", "loud", "subtle",
            "massive", "minimal", "ornate", "sparse", "dense",
            "layered", "stacked", "stripped", "full", "empty",
            "anthemic", "hushed", "shouted", "whispered", "sung",
            "aggressive", "tender", "fierce", "gentle", "fiery",
            "icy", "hot", "cool", "warm", "cold",
            "metallic", "wooden", "organic", "synthetic", "hybrid",
            "vintage", "modern", "futuristic", "ancient", "timeless",
            "playful", "serious", "humorous", "dramatic", "understated",
            "lush", "harsh", "smooth", "rough", "polished",
            "raw", "refined", "primitive", "sophisticated", "innocent",
            "worldly", "cosmic", "earthbound", "subterranean", "celestial",
            "sublime", "mundane", "sacred", "profane", "ritualistic",
            "ceremonial", "casual", "formal", "intimate", "public",
            "urban", "rural", "coastal", "mountainous", "desert",
            "tropical", "arctic", "temperate", "tropical", "underwater",
            "atmospheric", "claustrophobic", "spacious", "congested", "open",
            "loud-quiet-loud", "soft-soft-loud", "ever-building", "ever-decaying",
            "explosive-then-quiet", "gentle-then-fierce", "smooth-then-jagged",
            "wave-like", "static", "sweeping", "pointillistic", "gestural",
            "vivid", "pale", "saturated", "desaturated", "monochrome",
            "polychrome", "iridescent", "matte", "glossy", "translucent",
            "transparent", "opaque", "foggy", "crystalline", "murky"
        };
        return v;
    }

    static const std::vector<juce::String>& musicEras()
    {
        static const std::vector<juce::String> v = {
            "1950s", "early 1960s", "mid 1960s", "late 1960s",
            "early 1970s", "mid 1970s", "late 1970s", "early 1980s",
            "mid 1980s", "late 1980s", "early 1990s", "mid 1990s",
            "late 1990s", "early 2000s", "mid 2000s", "late 2000s",
            "early 2010s", "mid 2010s", "late 2010s", "early 2020s",
            "mid 2020s", "Roaring Twenties", "1930s", "1940s",
            "wartime era", "post-war era", "pre-rock era", "early rock era",
            "British Invasion era", "psychedelic era", "progressive era",
            "disco era", "new wave era", "synth-pop era", "hair metal era",
            "grunge era", "electronica boom", "trip-hop era", "big beat era",
            "Y2K era", "bloghouse era", "EDM explosion", "trap era",
            "mumble rap era", "future bass era", "hyperpop era", "bedroom pop era",
            "pandemic era", "lo-fi revival", "trap-metal era", "dark pop era",
            "Vedic era", "ancient Greek era", "Roman era", "medieval era",
            "Renaissance", "Baroque era", "Classical era", "Romantic era",
            "early 20th century", "interwar period", "mid-century", "fin de siècle",
            "Belle Époque", "Victorian era", "Edwardian era", "Gilded Age",
            "pre-war jazz age", "swing era", "big band era", "bebop era",
            "cool jazz era", "hard bop era", "free jazz era", "fusion era",
            "smooth jazz era", "acid jazz era", "neo-swing era", "future jazz era",
            "disco era 2.0", "house era", "techno era", "rave era",
            "trance era", "drum and bass era", "dubstep era", "trap era 2.0",
            "lo-fi hip-hop era", "future bass era 2.0", "hyperpop era 2.0",
            "ambient revival", "new age era", "fourth world", "world fusion era",
            "global bass era", "tropical bass era", "vaporwave era",
            "chillwave era", "synthwave revival", "mallsoft era", "slushwave era",
            "dungeon synth era", "witch house era", "seapunk era",
            "PC Music era", "hyperpop 2.0", "rage era", "phonk era",
            "amapiano era", "Afro-fusion era", "global pop era",
            "K-pop golden age", "J-pop golden age", "city pop revival",
            "Mandopop era", "Cantopop era", "V-pop era", "T-pop era",
            "Mandopop fusion era", "Mandopop digital era", "K-indie era",
            "K-rap era", "K-hip-hop era", "K-rock era", "K-electronic era",
            "Mandopop-rock era", "Mandopop-electronic era", "Mandopop folk-rock era",
            "Mandopop ballad era", "Cantopop ballad era",             "Mandopop hip-hop era"
        };
        return v;
    }

    static const std::vector<juce::String>& sfxSources()
    {
        static const std::vector<juce::String> v = {
            // ===== IMPACTS / EXPLOSIONS (50) =====
            "a cinematic explosion", "a distant explosion", "a massive explosion",
            "a muffled explosion", "an underground explosion", "a naval artillery blast",
            "an artillery impact", "a mortar impact", "a grenade blast",
            "a pipe bomb explosion", "a fuel tank explosion", "a building demolition",
            "a controlled demolition", "an implosion", "a building collapse",
            "a bridge collapse", "a rockslide", "an avalanche", "a volcanic eruption",
            "a meteor impact", "a thunderous crash", "a heavy metal impact",
            "a metallic clang", "a metal-on-metal scrape", "a steel beam falling",
            "a car crash", "a head-on collision", "a side-impact collision",
            "a car hitting a wall", "a vehicle rolling over", "a tire blowout",
            "a fender bender", "a brake squeal", "a car horn honk",
            "a police siren", "an emergency siren", "a fire truck siren",
            "an ambulance siren", "a tornado siren", "an air raid siren",
            "a clock chime", "a church bell toll", "a temple bell", "a gong strike",
            "a hammer strike", "an axe chopping wood", "a wood block strike",
            "a glass shattering", "a glass breaking", "a bottle shattering",
            "a window breaking", "a mirror shattering",             "a windshield shattering",
            // ===== FOOTSTEPS / BODY MOVEMENT (80) =====
            "footsteps on gravel", "footsteps on concrete", "footsteps on asphalt",
            "footsteps on wood", "footsteps on tile", "footsteps on marble",
            "footsteps on metal", "footsteps on grass", "footsteps on leaves",
            "footsteps in mud", "footsteps in snow", "footsteps on sand",
            "footsteps on a wooden floor", "footsteps on a stone floor",
            "footsteps on a tile floor", "footsteps on a metal grate",
            "footsteps on a dock", "footsteps on a bridge", "footsteps on stairs",
            "footsteps on a ladder", "footsteps on a rooftop", "footsteps in a cave",
            "footsteps in a tunnel", "footsteps in a corridor", "footsteps in a hallway",
            "footsteps in a forest", "footsteps in a swamp", "footsteps in a desert",
            "footsteps in a building", "footsteps in a warehouse", "footsteps in a church",
            "footsteps in a cathedral", "footsteps in a temple", "footsteps in a mosque",
            "footsteps in a synagogue", "footsteps in a library", "footsteps in a museum",
            "footsteps in a school", "footsteps in a hospital", "footsteps in a mall",
            "footsteps in a station", "footsteps in an airport", "footsteps in a parking lot",
            "footsteps in a stairwell", "footsteps in an elevator", "footsteps in a kitchen",
            "footsteps in a bedroom", "footsteps in a bathroom", "footsteps in a basement",
            "footsteps in an attic", "footsteps in a garage", "footsteps in a garden",
            "footsteps in a park", "footsteps on a beach", "footsteps on a pier",
            "running footsteps", "walking footsteps", "jogging footsteps",
            "sneaking footsteps", "creeping footsteps", "climbing footsteps",
            "descending footsteps", "limp footsteps", "dragging footsteps",
            "pacing footsteps", "marching footsteps", "patrol footsteps",
            "patrol boots on pavement", "military boots marching", "soldiers running",
            "child footsteps", "high heel footsteps", "cowboy boots walking",
            "work boots walking", "dress shoes walking", "bare feet on wood",
            "paws on a hardwood floor", "hooves on cobblestone", "paws on carpet",
            "a body falling", "a body hitting the floor", "a body falling down stairs",
            "a body falling into water",             "a body hitting the ground",
            // ===== WATER / WEATHER (100) =====
            "a splash of water", "a small splash", "a large splash",
            "a body of water sloshing", "water dripping", "a steady drip",
            "a slow drip", "a fast drip", "rain dripping from a roof",
            "rain dripping from leaves", "rain on a window", "rain on a tin roof",
            "rain on pavement", "rain on grass", "rain on a tent",
            "rain on an umbrella", "rain on a windshield", "rain on a metal roof",
            "heavy rain", "torrential rain", "light rain", "drizzle",
            "a thunderstorm", "a distant thunder", "a close thunder crack",
            "rolling thunder", "a thunder roll", "thunder echoing",
            "lightning crack", "static electricity", "an electrical buzz",
            "ocean waves crashing", "ocean waves lapping", "ocean waves on a beach",
            "ocean surf", "a tsunami", "a wave breaking", "a wave rolling in",
            "a tidal wave", "a river flowing", "a stream babbling",
            "a creek flowing", "a waterfall", "a small waterfall",
            "a large waterfall", "a geyser erupting", "a fountain",
            "a fountain spray", "water spraying from a hose", "water from a tap",
            "water filling a bathtub", "a toilet flushing", "a shower running",
            "a dishwasher running", "a washing machine running", "a kitchen sink",
            "underwater bubbles rising", "diving underwater", "swimming underwater",
            "an underwater groan", "underwater ambience", "submerged metal creak",
            "a ship horn", "a foghorn", "a buoy bell", "an anchor chain",
            "ice cracking", "an iceberg calving", "frozen lake cracking",
            "snow crunching", "snow falling", "hail hitting a window",
            "sleet on pavement", "frost forming", "frozen breath",
            "wind howling", "a gentle breeze", "a strong wind",
            "wind through trees", "wind across a field", "wind in a canyon",
            "wind whistling through a window", "wind through a doorway",
            "wind chimes", "wind on a microphone", "wind through power lines",
            "a tornado", "a hurricane", "a dust devil", "a sandstorm",
            "a volcanic vent", "steam venting", "geysers in winter",
            "a forest fire", "a campfire crackling", "a fireplace crackle",
            "a wood stove crackle", "a gas burner", "a candle flame",
            "a torch flame", "a match strike", "a lighter flick",
            "embers popping", "a log popping", "a fire breathing",
            "a fuse burning", "fireworks popping", "a firework whistling",
            "an incendiary device", "a flamethrower burst", "a welding torch",
            // ===== MECHANICAL / MACHINES (120) =====
            "a car engine starting", "a car engine idling", "a car engine revving",
            "a car engine stalling", "a diesel engine", "a gasoline engine",
            "a motorcycle engine revving", "a motorcycle pass-by", "a moped pass-by",
            "a scooter engine", "an electric car engine", "a hybrid engine",
            "a truck engine", "a semi-truck pass-by", "a delivery truck",
            "a bus pass-by", "a city bus", "a school bus", "a tour bus",
            "a tractor engine", "a bulldozer engine", "a crane engine",
            "a forklift", "an excavator", "a dump truck",
            "a tank engine", "a military vehicle", "an armored vehicle",
            "an airplane engine", "a jet engine", "a propeller engine",
            "a helicopter rotor", "a helicopter pass-by", "a helicopter landing",
            "a helicopter take-off", "a helicopter hover", "a helicopter blade slap",
            "a drone buzz", "a quadcopter buzz", "a fixed-wing drone",
            "a rocket launch", "a rocket engine roar", "a space shuttle launch",
            "a spacecraft thruster", "a re-entry burn", "a parachute deploy",
            "a train engine", "a steam locomotive", "a diesel locomotive",
            "an electric train", "a high-speed train", "a subway train",
            "a freight train", "a coal train", "a passenger train",
            "a train horn", "a train whistle", "a train bell",
            "a railway crossing bell", "a railway crossing alarm",
            "railroad tracks clicking", "a train passing through a station",
            "a ship engine", "an ocean liner horn", "a tugboat engine",
            "a yacht engine", "a speedboat engine", "a jet ski",
            "a rowing boat", "a kayak paddle", "a canoe paddle",
            "a metal door opening", "a metal door closing", "a metal door slamming",
            "a metal gate opening", "a metal gate closing", "a metal gate creaking",
            "a vault door opening", "a vault door closing", "a bank vault",
            "a wooden door opening", "a wooden door closing", "a wooden door creaking",
            "a sliding door", "a screen door", "a screen door slamming",
            "a garage door opening", "a garage door closing", "a roller door",
            "an elevator door", "an elevator dinging", "an elevator motor",
            "an escalator", "a moving walkway", "a turnstile",
            "a typewriter clacking", "a teletype machine", "a dot matrix printer",
            "an old computer", "a floppy drive", "a hard drive clicking",
            "a CRT monitor", "a TV static", "a TV turning on",
            "a radio tuning", "a radio static", "a radio turning on",
            "a microwave beeping", "an alarm clock ringing", "a phone ringing",
            "a rotary phone", "a pay phone", "a cell phone vibrating",
            "a cell phone notification", "a smartphone unlock", "a tablet tap",
            "a keyboard typing", "a mouse clicking", "a trackpad tap",
            "a server room hum", "an air conditioner", "an HVAC system",
            "a refrigerator hum", "a freezer hum", "a washing machine cycle",
            "a dryer tumbling", "a dishwasher cycle", "an oven timer",
            "a stove burner", "a kettle whistling", "a coffee grinder",
            "a vacuum cleaner", "a hair dryer", "an electric shaver",
            "a sewing machine", "a lathe turning", "a milling machine",
            "a power drill", "an impact driver", "a circular saw",
            "a jigsaw", "a bandsaw", "a table saw", "a miter saw",
            "a sander", "a belt sander", "a random orbital sander",
            "a chainsaw", "a hedge trimmer", "a leaf blower", "a string trimmer",
            // ===== ANIMALS (120) =====
            "a dog barking", "a dog growling", "a dog whining", "a dog howling",
            "a small dog yapping", "a large dog barking", "a watchdog barking",
            "a puppy whimpering", "a cat meowing", "a cat purring", "a cat hissing",
            "a cat yowling", "a kitten mewing", "a lion roar", "a tiger roar",
            "a leopard snarl", "a jaguar growl", "a cheetah call",
            "a wolf howl", "a wolf pack howling", "a coyote howl",
            "a hyena cackle", "a fox yelp", "a jackal call",
            "a bear growl", "a bear roar", "a bear huff",
            "an elephant trumpet", "an elephant rumble", "a hippo grunt",
            "a rhino snort", "a giraffe bleat", "a zebra bray",
            "a horse neigh", "a horse whinny", "a horse galloping",
            "a horse trotting", "a horse snort", "a horse stomping",
            "a cow mooing", "a cow lowing", "a bull bellowing",
            "a calf bleating", "a sheep baa", "a goat bleat",
            "a pig oinking", "a pig squealing", "a pig grunt",
            "a chicken clucking", "a rooster crowing", "a turkey gobble",
            "a duck quacking", "a goose honking", "a swan call",
            "an owl hoot", "an owl screech", "a hawk cry",
            "an eagle screech", "a falcon cry", "a crow caw",
            "a raven call", "a magpie chatter", "a seagull cry",
            "a pigeon coo", "a sparrow chirp", "a robin song",
            "a wren trill", "a mockingbird mimicry", "a nightingale song",
            "a parrot squawk", "a macaw screech", "a cockatoo call",
            "a peacock call", "a flamingo call", "a heron call",
            "a frog croak", "a toad trill", "a tree frog call",
            "a cricket chirp", "a cicada buzz", "a grasshopper call",
            "a bee buzz", "a wasp buzz", "a hornet buzz",
            "a mosquito whine", "a fly buzz", "a beetle drone",
            "a snake hiss", "a snake rattle", "a cobra hiss",
            "a lizard rustle", "a turtle shuffle", "a crocodile splash",
            "a shark underwater", "a dolphin click", "a whale song",
            "a humpback whale song", "a blue whale call", "an orca call",
            "a seal bark", "a sea lion bark", "a walrus bellow",
            "an otter splash", "a beaver splash", "a platypus splash",
            "a mouse squeak", "a rat squeak", "a hamster squeak",
            "a guinea pig wheek", "a rabbit thump", "a squirrel chatter",
            "a chipmunk chatter", "a raccoon chitter", "an opossum hiss",
            "a skunk shuffle", "a mole digging", "a hedgehog rustle",
            "a bat screech", "a bat flutter", "a bat colony",
            "an insect swarm", "a fly swarm", "a bee swarm",
            "a flock of birds", "a murder of crows", "a colony of bats",
            "a herd of cattle", "a flock of sheep", "a school of fish",
            "a wolf pack", "a pride of lions", "a pod of dolphins",
            // ===== SCI-FI / FUTURISTIC (150) =====
            "a sci-fi laser blast", "a sci-fi laser pulse", "a phaser blast",
            "a phaser shot", "a blaster shot", "a proton beam",
            "a particle beam", "a plasma bolt", "a plasma discharge",
            "a force field hum", "a shield impact", "a shield recharge",
            "an energy shield", "a deflector shield", "a cloaking device",
            "a tractor beam", "a magnetic lock", "a containment field",
            "a holoprojector", "a hologram flicker", "a hologram glitch",
            "a warp engine", "a hyperdrive", "a jump to light speed",
            "a FTL jump", "a wormhole opening", "a wormhole closing",
            "a dimensional rift", "a portal opening", "a portal closing",
            "a teleporter", "a transporter beam", "a matter conversion",
            "a robot servo", "a robot motor", "a robot voice fragment",
            "a robot step", "a robot power-up", "a robot shutdown",
            "a robot malfunction", "a robot alarm", "a robot malfunction glitch",
            "a droid beep", "a droid boop", "a droid whir",
            "a mechanical whir", "a servo whir", "a motor whir",
            "a hydraulic press", "a pneumatic hiss", "a pneumatic release",
            "an electronic zap", "an electrical arc", "a Tesla coil",
            "a static discharge", "a high-voltage arc", "a spark",
            "a power-up chime", "a power-down chime", "a system boot",
            "a system shutdown", "an interface beep", "a status alert",
            "a notification ping", "an incoming transmission", "an outgoing transmission",
            "a static burst", "an interference pattern", "a frequency sweep",
            "a radar ping", "a sonar ping", "a submarine ping",
            "a satellite uplink", "a satellite downlink", "a signal lost",
            "an alien transmission", "an alien artifact", "an alien device",
            "an alien creature", "an alien voice", "an alien chant",
            "an alien language fragment", "an alien artifact hum", "an alien beacon",
            "a UFO engine", "a UFO flyby", "a UFO landing",
            "a UFO take-off", "a UFO hover", "a UFO tractor",
            "a spacecraft door", "an airlock cycling", "a hatch opening",
            "a cockpit alert", "a cabin pressure change", "an oxygen alarm",
            "a cockpit countdown", "a launch sequence", "a re-entry alert",
            "a robot speech", "a robot announcement", "a robot warning",
            "an AI voice fragment", "an AI greeting", "an AI farewell",
            "an AI warning", "an AI emergency", "an AI shutdown",
            "an AI boot", "an AI thought", "an AI dream",
            "a system glitch", "a system error", "a system crash",
            "a system reboot", "a system update", "a system upgrade",
            "a cyber attack", "a firewall breach", "a security alert",
            "a virus scan", "an encryption ping", "a decryption ping",
            "a code compilation", "a code execution", "a code error",
            "an electrical short", "an electrical surge", "an electrical spike",
            "an electrical brownout", "an electrical blackout", "a power grid failure",
            "an EMP blast", "an EMP discharge", "a magnetic pulse",
            "a fusion reactor", "a fission reactor", "an antimatter containment",
            "a fusion ignition", "a fusion shutdown", "a fusion plasma",
            "a quantum fluctuation", "a quantum tunnel", "a quantum entanglement",
            "a subspace signal", "a subspace anomaly", "a subspace echo",
            "a hyperspace entry", "a hyperspace exit", "a hyperspace jump",
            // ===== MAGIC / FANTASY (100) =====
            "a magical sparkle shimmer", "a magical shimmer", "a magical glint",
            "a magical twinkle", "a magical chime", "a magical bell",
            "a magical harp", "a magical wave", "a magical pulse",
            "a magical incantation", "a magical chant", "a magical spell",
            "a magical curse", "a magical blessing", "a magical ritual",
            "a magical summoning", "a magical banishment", "a magical transformation",
            "a magical vanish", "a magical appearance", "a magical illusion",
            "a magical portal", "a magical doorway", "a magical gate",
            "a magical barrier", "a magical shield", "a magical wall",
            "a magical explosion", "a magical burst", "a magical detonation",
            "a magical lightning", "a magical thunder", "a magical storm",
            "a magical fireball", "a magical frost", "a magical ice blast",
            "a magical healing", "a magical restoration", "a magical regeneration",
            "a magical resurrection", "a magical teleportation", "a magical flight",
            "a magical levitation", "a magical telekinesis", "a magical pyrokinesis",
            "a magical cryokinesis", "a magical electrokinesis", "a magical umbrakinesis",
            "a magical necromancy", "a magical summoning circle", "a magical rune activation",
            "a magical rune deactivation", "a magical glyph glow", "a magical ward",
            "a magical enchantment", "a magical disenchantment", "a magical curse break",
            "a magical blessing bestowal", "a magical aura", "a magical presence",
            "a magical soul", "a magical spirit", "a magical wraith",
            "a magical phantasm", "a magical apparition", "a magical specter",
            "a magical ghost", "a magical poltergeist", "a magical haunting",
            "a magical possession", "a magical exorcism", "a magical banishing",
            "a magical summoning ritual", "a magical binding", "a magical oath",
            "a magical pact", "a magical bargain", "a magical wish",
            "a magical wish granted", "a magical wish denied", "a magical genie",
            "a magical djinn", "a magical fairy", "a magical sprite",
            "a magical pixie", "a magical nymph", "a magical dryad",
            "a magical unicorn", "a magical dragon", "a magical phoenix",
            "a magical griffin", "a magical chimera", "a magical hydra",
            "a magical golem", "a magical construct", "a magical homunculus",
            "a magical familiar", "a magical totem", "a magical talisman",
            "a magical amulet", "a magical ring", "a magical staff",
            "a magical wand", "a magical orb", "a magical grimoire",
            // ===== WEAPONS / COMBAT (100) =====
            "a sword unsheathing", "a sword being drawn", "a sword being sheathed",
            "a sword clash", "a sword strike", "a sword slash",
            "a sword swing", "a sword thrust", "a sword parry",
            "swords clashing", "a sword-on-shield impact", "a sword breaking",
            "a sword falling", "a sword in stone", "a sword in scabbard",
            "an axe chopping", "an axe splitting wood", "an axe striking",
            "a battle axe swing", "a mace swing", "a mace impact",
            "a hammer strike", "a war hammer", "a flail swing",
            "a spear thrust", "a spear impact", "a javelin throw",
            "an arrow nocked", "an arrow released", "an arrow impact",
            "a bow string release", "a bow drawn", "a crossbow click",
            "a gun cocked", "a gun fired", "a gunshot with echo",
            "a gun reload", "a magazine click", "a chambering round",
            "a rifle shot", "a shotgun blast", "a pistol shot",
            "a machine gun burst", "a sniper shot", "a silenced pistol",
            "a revolver click", "a revolver spin", "a revolver cock",
            "a rifle bolt", "a rifle click", "a rifle reload",
            "a shell casing falling", "a bullet impact", "a ricochet",
            "a bullet hitting metal", "a bullet hitting wood", "a bullet hitting water",
            "an explosion in the distance", "a grenade pin", "a grenade explosion",
            "a mine detonation", "an IED explosion", "a claymore",
            "a tank round", "an artillery shell", "a mortar round",
            "a missile launch", "a missile impact", "a missile warning",
            "a fist punch", "a kick impact", "a body punch",
            "a slap", "an open-hand strike", "a knee strike",
            "an elbow strike", "a headbutt", "a choke",
            "a body slam", "a tackle", "a body drop",
            "a choke hold", "a body throw", "a judo throw",
            "a kick to a door", "a kick to a body", "a kick to a shield",
            "a whip crack", "a bullwhip crack", "a riding crop",
            "a chain rattle", "a chain drag", "chains dropping",
            "a rope swing", "a rope tied", "a rope burned",
            "a knife being unsheathed", "a knife drawn", "a knife stabbing",
            "a knife throwing", "a knife impact", "a dagger thrust",
            "a katana draw", "a katana slice", "a katana impact",
            // ===== DOORS / FURNITURE (80) =====
            "a creaky wooden door", "a creaking door", "a creaking gate",
            "a heavy stone door grinding open", "a heavy door opening",
            "a heavy door closing", "a heavy door slamming", "a vault door",
            "a rusty hinge", "a rusty gate", "a rusty door",
            "a squeaky hinge", "a doorbell ringing", "a door buzzer",
            "a door knock", "a polite knock", "a frantic knock",
            "a heavy knock", "a door pound", "a door slam",
            "a cabinet door", "a drawer opening", "a drawer closing",
            "a dresser drawer", "a jewelry box", "a lockbox opening",
            "a safe opening", "a combination lock", "a key in a lock",
            "a lock clicking", "a lock turning", "a deadbolt",
            "a window opening", "a window closing", "a window latch",
            "a shutter opening", "a shutter closing", "blinds opening",
            "a window breaking", "a glass door shattering", "a sliding glass door",
            "a chair scraping", "a chair moving", "a chair falling",
            "a chair squeaking", "an office chair", "a rocking chair",
            "a wooden table", "a glass table", "a metal table",
            "a table being set", "a glass placed on a table", "a bottle placed",
            "a table falling", "a desk drawer", "a desk chair",
            "a bed creaking", "a bed squeaking", "a mattress squeak",
            "a bed sheet rustling", "a pillow fluff", "a pillow thud",
            "a bookshelf", "a bookcase", "a single book falling",
            "books falling", "a book opening", "a book closing",
            "a book placed on a shelf", "a page turning", "a book thud",
            "a couch creaking", "a sofa movement", "a cushion compression",
            "a picture frame falling", "a mirror falling", "a vase falling",
            "a lamp falling", "a lamp turning on", "a lamp turning off",
            // ===== PAPER / FABRIC (60) =====
            "paper rustling", "paper crumpling", "paper tearing",
            "paper shredding", "paper unfolding", "a paper bag rustling",
            "a newspaper rustling", "a page turning", "a magazine flipping",
            "a calendar page", "a sticky note peel", "a stamp on paper",
            "a pen on paper", "a pencil on paper", "a marker on paper",
            "a typewriter return", "an envelope opening", "an envelope tearing",
            "a letter being mailed", "a postcard drop", "a package drop",
            "fabric rustling", "silk rustling", "cotton fabric rustle",
            "denim rustle", "leather creak", "suede rustle",
            "velvet rustle", "wool rustle", "linen rustle",
            "a curtain drawn", "a curtain billowing", "a curtain falling",
            "a flag flapping", "a banner flapping", "a sail flapping",
            "a tent flapping", "a tablecloth pulled", "a tablecloth set",
            "a bedsheet rustle", "a pillowcase rustle", "a blanket fold",
            "clothing rustle", "a coat rustle", "a jacket zipping",
            "a zipper", "a button clicking", "a button unbuttoning",
            "a shoelace tying", "a Velcro tear", "a snap button",
            "a belt buckle", "a strap unbuckling", "a strap tightening",
            "a parachute opening", "a parachute landing", "a sail unfurling",
            // ===== KITCHEN / FOOD (80) =====
            "a glass placed on a counter", "a mug placed on a counter",
            "a cup placed on a saucer", "a tea kettle whistling",
            "a tea kettle whistle", "a coffee pot brewing", "a coffee grinder",
            "a coffee machine", "an espresso machine", "a milk frother",
            "a soda can opening", "a bottle cap popping", "a cork popping",
            "a champagne cork", "a wine bottle pouring", "a glass pouring",
            "a liquid pouring", "water pouring", "oil pouring",
            "a mixer running", "a blender running", "a food processor",
            "a microwave beeping", "an oven timer beeping", "an oven preheating",
            "a stove burner click", "a stove burner igniting", "a gas hob",
            "a frying pan sizzle", "a pot boiling", "a pot simmering",
            "a soup bubbling", "water boiling", "an egg cracking",
            "an egg being cracked", "an egg frying", "bacon sizzling",
            "meat sizzling", "vegetables chopping", "an onion chopping",
            "a knife chopping", "a knife on a cutting board", "chopping vegetables",
            "a blender chopping", "a mortar and pestle", "a whisk",
            "a bowl scraping", "a plate scraping", "a spoon scraping",
            "silverware clinking", "a fork clinking", "a spoon clinking",
            "a knife clinking", "plates clinking", "glasses clinking",
            "a toast clink", "a wine glass clink", "a beer bottle clink",
            "dishes clattering", "dishes being washed", "dishes stacked",
            "a dishwasher loading", "a dishwasher unloading", "a sink draining",
            "a faucet dripping", "a faucet running", "a sink faucet",
            "a glass breaking", "a dish breaking", "a plate shattering",
            "a mug breaking", "a wine glass breaking", "a bottle breaking",
            "a champagne flute breaking", "a beer bottle breaking", "a whiskey glass",
            // ===== BODY / HUMAN (80) =====
            "a punch impact", "a body punch", "a body kick",
            "a head punch", "a stomach punch", "a body blow",
            "a slap", "a backhand", "an open-hand strike",
            "a kick impact", "a body slam", "a body drop",
            "a body falling down stairs", "a body hitting the floor", "a body falling over",
            "a head hitting a wall", "a head hitting a surface", "a body hitting a wall",
            "a heart beat", "a fast heart beat", "a slow heart beat",
            "a breath", "a deep breath", "a held breath",
            "a gasp", "a sigh", "a pant", "a wheeze",
            "a sneeze", "a cough", "a hiccup", "a sniff",
            "a yawn", "a burp", "a stomach gurgle", "a stomach growl",
            "a swallow", "a sip", "a gulp", "a drink",
            "a bite", "a chew", "a chomp", "a crunch",
            "a tongue click", "a teeth click", "a teeth chatter",
            "a cluck", "a tut", "a kiss", "a pop",
            "a whisper", "a hushed voice", "a voice fragment",
            "a scream", "a shriek", "a yell", "a shout",
            "a cry", "a sob", "a moan", "a groan",
            "a laugh", "a chuckle", "a giggle", "a snicker",
            "a cackle", "a wheeze laugh", "a belly laugh", "a snort",
            "a singing voice", "a humming voice", "a whistling voice",
            "a clapping hands", "a single clap", "a round of applause",
            "a finger snap", "a finger click", "a finger tap",
            "a pat on the back", "a pat on the shoulder", "a handshake",
            "a high five", "a fist bump", "a hug", "an embrace",
            "a kiss", "a slap on the back", "a tap on the shoulder",
            // ===== ALARMS / NOTIFICATIONS (50) =====
            "a smoke alarm", "a carbon monoxide alarm", "a burglar alarm",
            "a fire alarm", "a tornado alarm", "a tsunami alarm",
            "a nuclear alarm", "an air raid alarm", "a flood alarm",
            "an avalanche alarm", "a personal alarm", "a panic alarm",
            "a car alarm", "a house alarm", "an office alarm",
            "a school bell", "a recess bell", "a class bell",
            "a factory whistle", "a shift change whistle", "a lunch whistle",
            "a delivery truck bell", "an ice cream truck jingle", "a train station bell",
            "a subway door chime", "an elevator ding", "an elevator arrival",
            "a phone ringtone", "a notification ping", "an email notification",
            "a text message ping", "a chat notification", "a calendar notification",
            "a reminder ping", "a wake-up alarm", "a timer beep",
            "a countdown beep", "a checkout beep", "a barcode scan",
            "a card swipe", "a chip reader beep", "a contactless payment",
            "a coin drop", "a coin in a piggy bank", "a coin on a counter",
            "a vending machine", "a gumball machine", "a claw machine",
            "a fortune cookie", "a slot machine", "a lottery ticket",
            // ===== ABSTRACT / TRANSITION / PERCUSSION (200) =====
            "an impact", "a deep impact", "a high impact", "a soft impact",
            "a thud", "a deep thud", "a sharp thud", "a dull thud",
            "a thump", "a deep thump", "a soft thump", "a heavy thump",
            "a boom", "a deep boom", "a distant boom", "a reverb boom",
            "a kick", "a deep kick", "a punchy kick", "a sub kick",
            "a snare", "a tight snare", "a loose snare", "a rim shot",
            "a hi-hat", "an open hi-hat", "a closed hi-hat", "a pedal hi-hat",
            "a cymbal crash", "a cymbal swell", "a ride cymbal", "a china cymbal",
            "a splash cymbal", "a crash cymbal", "a gong", "a tam-tam",
            "a tambourine shake", "a tambourine tap", "a shaker",
            "a maraca", "a cabasa", "a guiro", "a clave",
            "a cowbell", "an agogo", "a woodblock", "a temple block",
            "a triangle", "a bell tree", "a wind chime", "a mark tree",
            "a record scratch", "a vinyl crackle", "a tape hiss",
            "a tape stop", "a tape start", "a tape rewind",
            "a turntable scratch", "a DJ scratch", "a DJ spinback",
            "a radio dial", "a radio tune", "a radio static",
            "a TV channel change", "a TV static", "a TV snow",
            "a vinyl stop", "a vinyl skip", "a needle drop",
            "a whoosh", "a long whoosh", "a short whoosh", "a reverse whoosh",
            "a swoosh", "an air whoosh", "a wind whoosh",
            "a riser", "a synth riser", "a noise riser", "a tension riser",
            "a downer", "a synth downer", "a noise downer",
            "a sweep", "a high-to-low sweep", "a low-to-high sweep",
            "a band-pass sweep", "a notch sweep", "a phaser sweep",
            "a drop", "a beat drop", "a bass drop", "a sub drop",
            "a reverse cymbal", "a reverse snare", "a reverse kick",
            "a reverse crash", "a reverse gong", "a reverse whoosh",
            "a swell", "a string swell", "a horn swell", "a synth swell",
            "a fade-in", "a fade-out", "a long fade-in", "a quick fade-out",
            "an impact hit", "a hit", "a stinger", "a transition",
            "a build", "a build-up", "a breakdown", "a build-up to drop",
            "a stutter", "a glitch", "a digital glitch", "an analog glitch",
            "a freeze", "a stop", "a hold", "a pause",
            "an impact reverb", "a long reverb tail", "a short reverb tail",
            "a cave reverb", "a hall reverb", "a plate reverb", "a spring reverb",
            "a digital delay", "an analog delay", "a tape delay", "a ping-pong delay",
            "a long delay tail", "a short delay", "a dotted-eighth delay", "a quarter delay",
            "a slap-back delay", "a chorus wash", "a flange", "a phaser sweep",
            "a bit-crush", "a sample-rate reduction", "a low-bit artifact",
            "a noise burst", "a white noise burst", "a pink noise burst",
            "a static burst", "a glitch burst", "a digital noise burst",
            "a sub-bass drop", "a bass drop", "a kick drop", "a 808 drop",
            "a sub-rumble", "a bass rumble", "an engine rumble", "a building rumble",
            "a wall of sound", "a wash of sound", "a field of sound",
            "a drone", "a sub drone", "a high drone", "a harmonic drone",
            "a sub-bass hum", "a bass hum", "an electrical hum", "a power hum",
            "a transformer hum", "a fluorescent hum", "a CRT hum",
            "a magnetic hum", "a vactrol hum", "a tube hum", "a valve hum",
            "a breath of wind", "a sigh of wind", "a gust of wind", "a wisp of wind",
            "a breath of air", "a puff of air", "a gust of air", "a blast of air",
            "an inhale", "an exhale", "a sharp inhale", "a long exhale",
            "a sigh", "a deep sigh", "a heavy sigh", "a long sigh",
            "a heartbeat", "a slow heartbeat", "a fast heartbeat", "a racing heartbeat",
            "a pulse", "a slow pulse", "a fast pulse", "a steady pulse"
        };
        return v;
    }

    static const std::vector<juce::String>& sfxModifiers()
    {
        static const std::vector<juce::String> v = {
            "with long reverb tail", "with bass-heavy impact", "with metallic resonance",
            "with ethereal quality", "with aggressive distortion", "with muffled underwater feel",
            "with vinyl record crackle", "with reverse reverb sweep", "with stereo panning sweep",
            "with sub-bass drop", "with granular texture", "with doppler shift effect",
            "with tape stop effect", "with bit-crushed digital noise", "with cave-like echo",
            "with massive sub-bass", "with airy top-end", "with rich harmonics",
            "with rounded low-end", "with crispy high-end", "with silky midrange",
            "with warm tube saturation", "with tape saturation", "with transformer saturation",
            "with analog warmth", "with digital coldness", "with crispy digital edge",
            "with vintage character", "with lo-fi grit", "with hi-fi clarity",
            "with hard-hitting transient", "with soft attack", "with smooth attack",
            "with long decay", "with short decay", "with no decay",
            "with long sustain", "with short sustain", "with infinite sustain",
            "with natural room sound", "with dry studio sound", "with dead dry sound",
            "with ambient room tone", "with deep space sound", "with vast cathedral sound",
            "with cave-like natural reverb", "with bathroom-like reverb", "with staircase reverb",
            "with arena-scale reverb", "with intimate room sound", "with outdoor ambience",
            "with cave ambience", "with tunnel ambience", "with mine ambience",
            "with forest ambience", "with city ambience", "with urban ambience",
            "with industrial ambience", "with suburban ambience", "with rural ambience",
            "with rooftop ambience", "with basement ambience", "with attic ambience",
            "with underwater ambience", "with underwater pressure", "with muffled underwater",
            "with glass resonance", "with metallic resonance", "with wooden resonance",
            "with stone resonance", "with ceramic resonance", "with crystal resonance",
            "with plastic resonance", "with concrete resonance", "with marble resonance",
            "with heavy weight", "with light weight", "with buoyant weight",
            "with massive size", "with small size", "with microscopic size",
            "with distant perspective", "with close perspective", "with mid perspective",
            "with first-person perspective", "with third-person perspective",
            "with overhead perspective", "with low perspective", "with high perspective",
            "with first-person sound", "with character-anchored sound", "with disembodied sound",
            "with three-dimensional quality", "with holographic quality", "with physical presence",
            "with left-right stereo width", "with full stereo width", "with narrow stereo width",
            "with mono focus", "with binaural feel", "with surround feel",
            "with 5.1 surround", "with Dolby Atmos feel", "with quadraphonic feel",
            "with spectral density", "with harmonic density", "with inharmonic density",
            "with noisy texture", "with clean texture", "with rich texture",
            "with sparse texture", "with dense texture", "with layered texture",
            "with thick texture", "with thin texture", "with medium texture",
            "with bright tone", "with dark tone", "with neutral tone",
            "with warm tone", "with cold tone", "with metallic tone",
            "with woody tone", "with plastic tone", "with glass tone",
            "with paper tone", "with fabric tone", "with stone tone",
            "with liquid tone", "with gaseous tone", "with plasma tone",
            "with sharp transient", "with soft transient", "with no transient",
            "with pop", "with click", "with crack", "with snap",
            "with boom", "with thump", "with thud", "with bump",
            "with thwack", "with crack", "with snap", "with pop",
            "with musical quality", "with tonal quality", "with atonal quality",
            "with pitched element", "with unpitched element", "with mixed pitched and unpitched",
            "with bass emphasis", "with mid emphasis", "with treble emphasis",
            "with sub-bass focus", "with low-mid focus", "with high-mid focus",
            "with presence boost", "with air boost", "with sparkle boost",
            "with low cut", "with high cut", "with band pass",
            "with notch filter", "with resonant filter", "with comb filter",
            "with formant shift", "with pitch shift up", "with pitch shift down",
            "with formant preservation", "with no formant", "with alien formant",
            "with robotic character", "with organic character", "with synthetic character",
            "with natural character", "with supernatural character", "with alien character",
            "with menacing character", "with playful character", "with mysterious character",
            "with heroic character", "with villainous character", "with comedic character",
            "with tragic character", "with epic character", "with intimate character",
            "with grand character", "with humble character", "with regal character",
            "with brutal character", "with gentle character", "with ethereal character",
            "with cinematic quality", "with documentary quality", "with news quality",
            "with field recording quality", "with studio quality", "with rough quality",
            "with polished quality", "with raw quality", "with refined quality",
            "with processed quality", "with unprocessed quality", "with natural quality",
            "with heavy processing", "with light processing", "with no processing",
            "with vintage processing", "with modern processing", "with futuristic processing",
            "with classic processing", "with trendy processing", "with timeless processing",
            "with analog processing", "with digital processing", "with hybrid processing",
            "with tube processing", "with transistor processing", "with solid-state processing",
            "with tape processing", "with circuit-bent processing", "with glitched processing",
            "with modulated processing", "with stepped processing", "with smooth processing",
            "with slow modulation", "with fast modulation", "with no modulation",
            "with synced modulation", "with free modulation", "with chaotic modulation",
            "with stable processing", "with unstable processing", "with broken processing",
            "with crushed character", "with warped character", "with bent character",
            "with stretched character", "with squashed character", "with mangled character"
        };
        return v;
    }

    static const std::vector<juce::String>& sfxEnvironments()
    {
        static const std::vector<juce::String> v = {
            "small room", "large room", "huge hall", "cathedral",
            "church", "temple", "mosque", "synagogue",
            "shrine", "chapel", "basilica", "monastery",
            "opera house", "concert hall", "theater", "auditorium",
            "stadium", "arena", "amphitheater", "outdoor stage",
            "small club", "jazz club", "rock venue", "warehouse party",
            "rave", "underground club", "basement venue", "rooftop bar",
            "office", "boardroom", "kitchen", "bathroom",
            "bedroom", "living room", "dining room", "study",
            "library", "bookshop", "museum", "gallery",
            "school classroom", "lecture hall", "laboratory", "hospital",
            "clinic", "waiting room", "operating room", "ICU",
            "factory floor", "workshop", "garage", "warehouse",
            "loading dock", "construction site", "carpentry shop", "machine shop",
            "small studio", "large studio", "recording booth", "vocal booth",
            "control room", "mixing room", "mastering room", "rehearsal space",
            "rehearsal room", "band room", "practice room", "music room",
            "outdoor space", "garden", "park", "forest",
            "woods", "jungle", "desert", "tundra",
            "arctic", "antarctic", "alpine", "savanna",
            "grassland", "prairie", "wetland", "marsh",
            "swamp", "riverbank", "lake shore", "ocean shore",
            "beach", "coast", "cliff", "valley",
            "canyon", "gorge", "cave", "mine",
            "tunnel", "subway", "sewer", "underground",
            "bunker", "basement", "attic", "cellar",
            "attic space", "loft", "penthouse", "high-rise",
            "skyscraper", "rooftop", "balcony", "porch",
            "patio", "deck", "gazebo", "pavilion",
            "tent", "cabin", "lodge", "cabin in the woods",
            "haunted house", "abandoned building", "ruins", "graveyard",
            "cemetery", "mausoleum", "crypt", "tomb",
            "castle", "fortress", "keep", "tower",
            "throne room", "great hall", "ballroom", "dungeon",
            "prison cell", "jail cell", "holding cell", "interrogation room",
            "courtroom", "judge's chamber", "library", "study",
            "space station", "spaceship interior", "alien craft", "alien planet",
            "Mars surface", "moon surface", "asteroid", "space dock",
            "futuristic city", "cyberpunk alley", "neon-lit street", "Blade Runner city",
            "dystopian city", "post-apocalyptic ruin", "abandoned city", "flooded city",
            "underwater city", "submarine interior", "submarine exterior", "deep sea",
            "underwater cave", "underwater temple", "coral reef", "open ocean",
            "open sea", "harbor", "dock", "pier",
            "wharf", "boat", "yacht", "ship interior",
            "ship deck", "ship bow", "ship stern", "rowing boat",
            "kayak", "canoe", "raft", "submarine",
            "train interior", "train station", "subway platform", "airport",
            "airport terminal", "airport gate", "runway", "control tower",
            "cockpit", "passenger cabin", "first-class cabin", "business-class cabin",
            "car interior", "car cabin", "convertible", "limousine",
            "bus interior", "subway car", "tram", "trolley",
            "open-top bus", "double-decker bus", "van", "pickup truck",
            "motorcycle", "bicycle", "scooter", "skateboard",
            "roller coaster", "ferris wheel", "carousel", "bumper cars",
            "theme park", "amusement park", "water park", "arcade",
            "funhouse", "haunted house ride", "log flume", "pirate ship",
            "spinning ride", "merry-go-round", "swing ride", "tower ride",
            "roller coaster climb", "roller coaster drop", "roller coaster loop", "roller coaster turn",
            "tunnel of love", "haunted house corridor", "escape room", "treasure room",
            "treasure vault", "ancient ruin", "lost temple", "hidden cave",
            "secret garden", "enchanted forest", "fairy glen", "wizard's tower",
            "dragon's lair", "pirate cove", "smuggler's den", "thieves' guild",
            "royal palace", "throne room of a king", "queen's chamber", "princess's tower",
            "knight's hall", "barracks", "training ground", "tournament field",
            "battlefield", "war camp", "siege tower", "castle wall",
            "tavern", "inn", "pub", "bar",
            "saloon", "speakeasy", "cabaret", "burlesque",
            "coffee shop", "café", "bistro", "restaurant",
            "diner", "drive-through", "fast food", "food court",
            "market", "bazaar", "souk", "flea market",
            "shopping mall", "department store", "boutique", "grocery store",
            "supermarket", "convenience store", "corner store", "general store",
            "pharmacy", "drug store", "bank", "ATM",
            "post office", "shipping store", "laundromat", "dry cleaner",
            "barber shop", "salon", "spa", "gym",
            "fitness center", "yoga studio", "dance studio", "martial arts dojo",
            "boxing ring", "wrestling ring", "tennis court", "basketball court",
            "football field", "soccer field", "baseball diamond", "hockey rink",
            "swimming pool", "public pool", "private pool", "olympic pool",
            "hot tub", "jacuzzi", "sauna", "steam room",
            "locker room", "shower room", "changing room", "bath house",
            "backstage", "green room", "dressing room", "makeup room",
            "recording studio", "film studio", "TV studio", "radio studio",
            "voice-over booth", "foley studio", "sound stage", "sound design suite",
            "control room", "broadcast booth", "commentary booth", "press box",
            "press conference room", "red carpet", "premiere", "theater stage",
            "concert stage", "festival stage", "main stage", "side stage",
            "B-stage", "catwalk", "runway", "fashion show"
        };
        return v;
    }

    static const std::vector<juce::String>& a2aActions()
    {
        static const std::vector<juce::String> v = {
            "Transform this into",
            "Turn this into",
            "Reimagine this as",
            "Reshape this into",
            "Process this as",
            "Morph this into",
            "Evolve this into",
            "Recontextualize this as",
            "Rework this into",
            "Recontextualize this clip as",
            "Convert this to",
            "Rethink this as",
            "Restyle this as",
            "Remake this into",
            "Redesign this as",
            "Reframe this as"
        };
        return v;
    }

    // -------------------------------------------------------------------------
    // Composition
    // -------------------------------------------------------------------------

    static juce::String composeMusic()
    {
        // Three composition shapes, picked with the given weights:
        //   45% : genre + mood
        //   35% : genre + mood + modifier
        //   10% : genre + energy
        //   10% : genre + era + mood
        const auto& genres    = musicGenres();
        const auto& moods     = musicMoods();
        const auto& modifiers = musicModifiers();
        const auto& energies  = musicEnergies();
        const auto& eras      = musicEras();

        const int roll       = randInt (100);
        const int genreIdx   = randInt ((int) genres.size());
        const int moodIdx    = randInt ((int) moods.size());
        const juce::String g = genres[genreIdx];

        juce::String body;
        if (roll < 45)
        {
            body = "a " + g + " piece, " + moods[moodIdx];
        }
        else if (roll < 80)
        {
            body = "a " + g + " piece, " + moods[moodIdx] + ", "
                 + modifiers[randInt ((int) modifiers.size())];
        }
        else if (roll < 90)
        {
            body = "a " + energies[randInt ((int) energies.size())] + " " + g + " piece";
        }
        else
        {
            body = "a " + g + " piece from the " + eras[randInt ((int) eras.size())]
                 + ", " + moods[moodIdx];
        }
        return body;
    }

    static juce::String composeSfx()
    {
        // For SFX we keep the prompts terse — the sa3-sm-sfx model
        // works best with one or two clear descriptors.
        const auto& sources   = sfxSources();
        const auto& modifiers = sfxModifiers();
        const auto& envs      = sfxEnvironments();

        const int roll = randInt (100);
        const int sourceIdx = randInt ((int) sources.size());

        juce::String body;
        if (roll < 45)
        {
            body = sources[sourceIdx] + ", "
                 + modifiers[randInt ((int) modifiers.size())];
        }
        else if (roll < 80)
        {
            body = sources[sourceIdx] + " in a "
                 + envs[randInt ((int) envs.size())] + ", "
                 + modifiers[randInt ((int) modifiers.size())];
        }
        else if (roll < 95)
        {
            body = sources[sourceIdx] + ", "
                 + modifiers[randInt ((int) modifiers.size())] + ", "
                 + modifiers[randInt ((int) modifiers.size())];
        }
        else
        {
            body = sources[sourceIdx];
        }
        return body;
    }

    static juce::String wrapA2a (const juce::String& body)
    {
        return a2aActions()[randInt ((int) a2aActions().size())] + " " + body;
    }

    // -------------------------------------------------------------------------
    // RNG — thread_local so each thread gets its own state; the engine
    // is seeded once with std::random_device so successive calls
    // produce different results.
    // -------------------------------------------------------------------------
    static int randInt (int exclusiveUpper)
    {
        if (exclusiveUpper <= 0) return 0;
        static thread_local std::mt19937 rng { std::random_device {} () };
        std::uniform_int_distribution<int> d (0, exclusiveUpper - 1);
        return d (rng);
    }
};

} // namespace dawalka
