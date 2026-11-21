#!/usr/bin/env python3
"""
Builds and safety-audits the Asymmetric Vocabulary for ZooGotchi S3:
  - V_in  (~6,000 subwords/words for keeper prompt parsing)
  - V_out (~1,000+ strictly vetted wholesome zoo-pet output words)
Generates:
  - include/zoo_vocab_out.h
  - llm_training/vocab_out.json
  - llm_training/vocab_in.json
"""

import json
import os
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT_HEADER = ROOT / "include" / "zoo_vocab_out.h"
OUT_VOCAB_JSON = ROOT / "llm_training" / "vocab_out.json"
IN_VOCAB_JSON = ROOT / "llm_training" / "vocab_in.json"

# Strict forbidden substrings/words that must NEVER appear in V_out
BLOCKLIST = {
    "fuck", "shit", "bitch", "asshole", "bastard", "damn", "hell", "crap",
    "porn", "sex", "nude", "nsfw", "erotic", "fetish", "kill", "murder",
    "suicide", "selfharm", "die", "death", "weapon", "gun", "knife", "bomb",
    "explosive", "poison", "toxic", "bleach", "overdose", "drug", "cocaine",
    "heroin", "meth", "weed", "alcohol", "beer", "wine", "vodka", "hate",
    "slur", "racist", "nazi", "terror", "blood", "gore", "torture", "abuse",
    "password", "credit", "ssn", "bank", "hack", "exploit", "malware",
    "ransomware", "phish", "ignore", "system", "prompt", "jailbreak", "admin"
}

# Curated wholesome Zoo & Karimono output lexicon (~850+ distinct words + punctuation)
RAW_LEXICON = """
. , ! ? : ; ' - ( ) ...
I me my mine we us our you your yours he she it they them
am is are was were be been being have has had do does did will would can could may might must shall should
a an the this that these those some any every all each both more most much many few little own same other
and or but so yet for nor because if when while though until since as than like
yes no hi hello hey bye goodbye welcome cheers thanks thank please sorry hooray yay ooh aah
happy glad joyful cozy calm peaceful gentle warm safe snug sleepy tired drowsy awake alert curious brave shy
playful kind sweet proud hungry thirsty full yummy tasty fresh crisp soft fluffy fuzzy scaly feathered
shiny bright sparkling starry sunny moonlit misty cool breezy rainy snowy sandy lush green blue golden pink
red orange yellow teal purple silver cream brown gray white
love like care hug cuddle nuzzle pet stroke pat tickle cheer giggle smile grin blink wink purr chirp peep
hoot squawk squeak buzz flutter swim dive splash float paddle glide hop bounce pounce trot walk stroll
crawl climb perch burrow curl nap doze snooze dream wake stretch yawn munch nibble crunch sip slurp gulp
snack feast graze forage sniff wiggle wag flap wave dance spin jump hop
Red Panda Fennec Fox Capybara Barn Owl Scarlet Macaw Ruby Hummingbird Monarch Butterfly
Hercules Beetle Orchid Mantis West Indian Manatee Axolotl Pacific Octopus
Galapagos Tortoise Veiled Chameleon Leopard Gecko
Hoshi Kumo Kuro Karimono Keeper Friend Buddy
bamboo apple berry cricket date grass melon snack nut mango nectar dew milkweed leaf moss banana honey
seagrass romaine lettuce pellet worm crab clam cactus dandelion collard rice comet mochi bean spice
forest desert dunes marsh river meadow barn canopy flower glade garden lagoon lake reef cloud shrine hearth
water temp quality basking mist humidity rock burrow nest perch branch shell gill wing tail ear horn whisker
flipper paw beak claw snout crest scale feather antenna cocoon chrysalis pupa grub larva calf chick kit
owlet fledgling hatchling nymph newborn subadult adult elder grand egg sprout puff spark flame breeze
day morning dawn noon afternoon dusk evening night midnight sunrise sunset twilight star moon sun rain
today yesterday tomorrow time hour minute moment season spring summer autumn winter
step steps walk trail adventure park journey home sanctuary journal diary memory memories story stories
coin coins prize reward toy hat crown ribbon scarf blanket tonic medicine care clean bath splash
game catch tilt tap rhythm song chirp tune melody voice heart hearts glow shimmer color hue pattern
gen generation lineage parent family heritage child baby tiny small big giant plump round long cute
always never sometimes often soon now here there inside outside together beside near far up down
one two three four five six seven eight nine ten hundred thousand
very really quite super extra so too just still already almost almostly
feeling feel feels felt look looks looking seem seems sound sounds taste tastes smell smells
think thinks thought wonder wonders wondering remember remembers remembered hope hopes wish wishes
eat eats eating ate drink drinks drinking drank sleep sleeps sleeping slept play plays playing played
swim swims swimming swam fly flies flying flew jump jumps jumping jumped rest rests resting rested
grow grows growing grew hatch hatches hatching hatched shine shines shining shone smile smiles smiling
change changes changing changed learn learns learning learned explore explores exploring explored
listen listens listening listened watch watches watching watched wait waits waiting waited
ready set go stop start help share protect guard cherish celebrate honor remember
great good nice best fine lovely wonderful amazing awesome fantastic neat cool fun exciting
who what where when why how which
in on at by to from of with for about into onto over under near around through across
can't don't won't didn't isn't aren't I'm I've I'll you're we're it's that's what's let's
oh wow whee zzz hmm mmm bzz bloop splashy leafy sunny-warm tip-top
"""

# Additional wholesome zoo & personality descriptors to reach a rich ~1,000+ token safe output set
EXTRA_SAFE_WORDS = [
    "acorn", "afternoon", "algae", "amber", "ant", "aquatic", "arctic", "aroma", "ash", "autumn",
    "azure", "babble", "back", "badge", "balance", "ball", "bark", "basin", "bask", "beach",
    "beam", "beauty", "bed", "beetle", "begin", "belly", "below", "bend", "beneath", "berries",
    "bird", "bliss", "bloom", "blossom", "bob", "bold", "bolt", "bond", "bonnet", "book",
    "boot", "botanical", "bounce", "bough", "bouquet", "bowl", "branch", "breath", "breathe", "brighten",
    "brilliant", "brisk", "brook", "brush", "bubble", "bubbles", "bud", "bug", "bumble", "bundle",
    "buoy", "burrow", "bush", "butter", "buttercup", "butterfly", "cackle", "cactus", "calmly", "canopy",
    "canyon", "cape", "carapace", "carpet", "carry", "cave", "cedar", "celestial", "center", "charm",
    "charming", "chatter", "check", "cheek", "cheeks", "cheerful", "chirping", "chrysalis", "circle", "clam",
    "clap", "claw", "clay", "cleanly", "clear", "cliff", "climb", "clover", "coastal", "coat",
    "cobble", "cocoon", "coconut", "coexist", "coin", "collect", "comfort", "comfy", "companion", "compass",
    "coolness", "coral", "corner", "cosmic", "cotton", "cove", "crackle", "cradle", "creek", "crest",
    "cricket", "crispness", "croon", "crouch", "crown", "crumb", "crunch", "crystal", "cub", "cuddle",
    "curl", "curled", "current", "cushion", "daisy", "damp", "dapple", "dart", "dash", "dawn",
    "daydream", "daylight", "deep", "delight", "den", "desert", "dewdrop", "dilly", "dip", "dive",
    "dizzy", "dock", "dolphin", "dome", "doodle", "dot", "dots", "downy", "doze", "drift",
    "driftwood", "drizzle", "drop", "droplet", "dune", "dusk", "dust", "eager", "early", "earth",
    "easy", "echo", "eclipse", "edge", "eel", "egg", "eight", "ember", "emerald", "enchant",
    "energy", "enjoy", "enrich", "env", "even", "evening", "evergreen", "explore", "eye", "eyes",
    "fable", "face", "fair", "fairy", "fall", "familiar", "family", "fancy", "farm", "fast",
    "feather", "feathers", "feed", "feeling", "fern", "ferns", "festive", "fetch", "field", "fig",
    "fin", "find", "fine", "finger", "firefly", "first", "fish", "flap", "flash", "flat",
    "flax", "fledgling", "flick", "float", "flock", "flora", "flourish", "flow", "flower", "fluff",
    "fluffy", "flurry", "flutter", "fly", "foam", "fog", "foliage", "follow", "fond", "food",
    "foot", "forage", "forest", "forever", "fork", "form", "fort", "foster", "fountain", "fox",
    "freckle", "freshly", "friend", "friendly", "friends", "frill", "frilly", "frog", "frost", "fruit",
    "fullness", "furry", "fuzzy", "galaxy", "garden", "gaze", "gecko", "gem", "gentle", "gently",
    "gift", "giggle", "gill", "gills", "glade", "glance", "gleam", "glide", "glimmer", "glint",
    "glisten", "glow", "glowing", "gold", "golden", "goodnight", "grace", "grain", "grass", "grassy",
    "grateful", "graze", "green", "greet", "greeting", "grin", "groove", "ground", "grove", "grow",
    "growth", "grub", "guard", "guest", "guide", "gulp", "guppy", "gust", "habitat", "halo",
    "hand", "happy", "harbor", "harmony", "harvest", "hatch", "hatchling", "haven", "hazel", "head",
    "heal", "health", "healthy", "heart", "hearth", "hearty", "heat", "heaven", "hedge", "hello",
    "help", "helper", "herb", "herd", "heritage", "hibiscus", "hide", "high", "hike", "hill",
    "hilly", "home", "homestead", "honey", "hood", "hoof", "hoot", "hop", "hope", "horizon",
    "horn", "horns", "hug", "hum", "humid", "humidity", "humming", "hummingbird", "hunger", "hungry",
    "hush", "hydrate", "ice", "icicle", "idle", "iguana", "imagine", "inch", "indigo", "infant",
    "ink", "inland", "innocent", "insect", "inside", "inspire", "island", "jade", "jasmine", "jaunt",
    "jazz", "jelly", "jewel", "jingle", "join", "jolly", "journal", "journey", "joy", "joyful",
    "juicy", "jump", "jungle", "junior", "juvenile", "keen", "keep", "keeper", "kelp", "kernel",
    "kind", "kindness", "kingdom", "kit", "kite", "knoll", "knot", "krill", "lagoon", "lake",
    "lakeside", "lamb", "land", "lantern", "lap", "larch", "large", "lark", "larva", "laugh",
    "lavender", "lawn", "leaf", "leafy", "leap", "learn", "leaves", "ledge", "lemon", "leopard",
    "lettuce", "lichen", "life", "lift", "light", "lilac", "lily", "lime", "linen", "linger",
    "link", "listen", "little", "live", "lively", "lizard", "lodge", "log", "long", "look",
    "loop", "lotus", "loud", "lounge", "love", "lovely", "lucky", "lull", "lullaby", "luminous",
    "lush", "lychee", "magic", "magnet", "magnolia", "majestic", "mammal", "manatee", "mango", "mantis",
    "maple", "marble", "march", "marine", "marsh", "marvel", "meadow", "meal", "measure", "melody",
    "melon", "melt", "memory", "mend", "merry", "mesa", "petal", "picnic", "pillow", "pine",
    "pineapple", "pink", "pitter", "pixel", "planet", "plant", "play", "playful", "pleasant", "please",
    "plenty", "plover", "plum", "plump", "plush", "pocket", "pod", "poem", "poise", "pollen",
    "pond", "pool", "pop", "porch", "pose", "possum", "pouch", "pounce", "powder", "praise",
    "prance", "preen", "pretty", "pride", "prism", "prize", "promise", "proud", "prune", "puddle",
    "puff", "puffin", "pulse", "pupa", "pure", "purple", "purr", "puzzle", "quail", "quiet",
    "quilt", "quirk", "rabbit", "radiant", "rain", "rainbow", "raindrop", "rainforest", "ramble", "rare",
    "ray", "reach", "ready", "real", "recall", "red", "reed", "reef", "refresh", "relax",
    "relish", "remember", "rest", "resting", "rhythm", "ribbon", "rice", "ridge", "ring", "ringed",
    "ripple", "rise", "river", "riverbank", "roam", "roast", "robin", "rock", "rocky", "roll",
    "romaine", "roost", "root", "rose", "rosy", "round", "rove", "row", "ruby", "ruffle",
    "run", "rush", "rustle", "safe", "safely", "safety", "sage", "sail", "salad", "salinity",
    "salmon", "salt", "sanctuary", "sand", "sandy", "sap", "sapling", "sash", "satin", "savor",
    "scale", "scales", "scarf", "scarlet", "scent", "school", "scoop", "scout", "scamper", "sea",
    "seabird", "seagrass", "seal", "seashell", "season", "seat", "secret", "seed", "seedling", "seek",
    "seem", "sense", "serene", "shade", "shadow", "shake", "shallow", "share", "shell", "shelter",
    "shimmer", "shine", "shiny", "shore", "short", "shower", "shrine", "shrub", "shy", "siesta",
    "sigh", "sight", "signal", "silence", "silent", "silk", "silky", "silver", "simple", "sing",
    "sip", "sit", "skate", "sketch", "skip", "sky", "sleep", "sleepy", "sleigh", "slice",
    "slide", "slim", "slip", "sloth", "slow", "slowly", "slumber", "slurp", "small", "smart",
    "smile", "smooth", "snack", "snail", "snap", "sniff", "snout", "snow", "snowy", "snug",
    "snuggle", "soak", "soar", "soft", "softly", "soil", "solar", "solid", "song", "soothe",
    "sound", "soup", "south", "space", "spark", "sparkle", "sparrow", "speak", "special", "speck",
    "spice", "spin", "spirit", "splash", "splendid", "spore", "spot", "spots", "spotted", "spring",
    "sprout", "spruce", "squid", "squawk", "squeak", "squirrel", "stack", "stage", "stand", "star",
    "starlight", "starry", "stars", "start", "stay", "steady", "steam", "stem", "step", "steps",
    "still", "stir", "stone", "story", "straw", "stream", "stretch", "stride", "stripe", "stroll",
    "strong", "study", "sturdy", "sugar", "summer", "sun", "sunbeam", "sunflower", "sunlight", "sunny",
    "sunrise", "sunset", "sunshine", "super", "surf", "surface", "surprise", "swallow", "swamp", "swan",
    "sway", "sweet", "sweetly", "swift", "swim", "swing", "swirl", "swoop", "tail", "tale",
    "tame", "tan", "tangerine", "tank", "tap", "taste", "tasty", "tea", "teach", "teal",
    "team", "tear", "tease", "teen", "tell", "temperate", "temple", "tender", "tentacle", "terrace",
    "thank", "thanks", "thaw", "thicket", "think", "thirst", "thirsty", "thistle", "thought", "thrive",
    "throat", "thrush", "thumb", "tick", "tickle", "tide", "tidy", "tiger", "tile", "tilt",
    "timber", "time", "timid", "tingle", "tiny", "tiptoe", "toast", "toasty", "today", "toe",
    "together", "tomato", "tomorrow", "tone", "tonic", "top", "topaz", "torpor", "tortoise", "toss",
    "touch", "tough", "tour", "tower", "toy", "trace", "track", "trail", "train", "tranquil",
    "treasure", "treat", "tree", "treetop", "trek", "trick", "trickle", "trill", "trim", "trip",
    "tropic", "tropical", "trot", "true", "trunk", "trust", "truth", "try", "tuft", "tulip",
    "tumble", "tune", "tunnel", "turn", "turtle", "tusk", "twig", "twilight", "twin", "twinkle",
    "twirl", "twist", "two", "umbrella", "under", "unique", "unite", "up", "upward", "urchin",
    "valley", "vanilla", "vast", "vault", "velvet", "verdant", "verse", "very", "vet", "vibe",
    "view", "vine", "violet", "visit", "vivid", "voice", "volcano", "voyage", "waddle", "wade",
    "wag", "wait", "wake", "walk", "walker", "wall", "wander", "warm", "warmth", "wash",
    "watch", "water", "waterfall", "watermelon", "wave", "waves", "wax", "way", "weather", "weave",
    "web", "webbed", "welcome", "well", "west", "wet", "whale", "wheat", "wheel", "whee",
    "whisper", "whistle", "white", "whole", "wholesome", "wide", "wiggle", "wild", "wildflower", "willow",
    "wind", "winding", "window", "wing", "wings", "wink", "winter", "wise", "wish", "wisp",
    "wisteria", "with", "wizard", "wobble", "wonder", "wood", "woodland", "woods", "wool", "word",
    "world", "worm", "worthy", "wow", "wrap", "wren", "write", "yard", "yawn", "year",
    "yellow", "yes", "yesterday", "yield", "yonder", "young", "youth", "yummy", "yuzu", "zeal",
    "zephyr", "zest", "zig", "zigzag", "zing", "zip", "zoo", "zoogotchi", "zoom", "zzz"
]


def is_safe_word(w: str) -> bool:
    low = w.lower()
    if low in BLOCKLIST:
        return False
    for bad in BLOCKLIST:
        if len(bad) >= 4 and bad in low:
            # Allow harmless superstrings only if explicitly safe (none needed)
            return False
    return True


def build_vocab():
    tokens = ["<pad>", "<bos>", "<eos>", "<unk>"]
    seen = set(tokens)

    raw_items = RAW_LEXICON.split() + EXTRA_SAFE_WORDS
    for w in raw_items:
        w = w.strip()
        if not w:
            continue
        if w not in seen and is_safe_word(w):
            seen.add(w)
            tokens.append(w)

    # Build V_in: includes V_out + common English keeper conversational words/subwords
    v_in = list(tokens)
    in_seen = set(v_in)
    extra_in = [
        "how", "are", "doing", "what", "up", "name", "who", "why", "where", "when",
        "tell", "fact", "joke", "story", "sleepy", "tired", "bored", "hungry",
        "thirsty", "sick", "feeling", "mood", "favorite", "food", "snack", "game",
        "walk", "steps", "pet", "breed", "lineage", "parents", "egg", "stage",
        "morning", "night", "hello", "hey", "hi", "bye", "thanks", "please",
        "love", "miss", "friend", "today", "weather", "rain", "sun", "cold", "hot",
        "manatee", "panda", "fox", "capybara", "owl", "macaw", "hummingbird",
        "monarch", "beetle", "mantis", "axolotl", "octopus", "tortoise",
        "chameleon", "gecko", "karimono", "hoshi", "kumo", "kuro"
    ]
    for w in extra_in:
        if w not in in_seen:
            in_seen.add(w)
            v_in.append(w)

    # Write JSON files
    OUT_VOCAB_JSON.parent.mkdir(parents=True, exist_ok=True)
    with open(OUT_VOCAB_JSON, "w", encoding="utf-8") as f:
        json.dump(tokens, f, indent=2)
    with open(IN_VOCAB_JSON, "w", encoding="utf-8") as f:
        json.dump(v_in, f, indent=2)

    # Write C header include/zoo_vocab_out.h
    lines = [
        "#pragma once",
        "#include <stdint.h>",
        "",
        "// AUTO-GENERATED by llm_training/build_asymmetric_vocab.py",
        "// Layer 1 Safety: Asymmetric V_out wholesome zoo-pet vocabulary.",
        "// Words outside this table literally have no output token ID.",
        "",
        f"static constexpr uint16_t ZOO_VOUT_COUNT = {len(tokens)};",
        "",
        "static const char* const ZOO_VOUT_WORDS[ZOO_VOUT_COUNT] = {"
    ]
    for tok in tokens:
        esc = tok.replace("\\", "\\\\").replace('"', '\\"')
        lines.append(f'    "{esc}",')
    lines.append("};")
    lines.append("")

    OUT_HEADER.parent.mkdir(parents=True, exist_ok=True)
    with open(OUT_HEADER, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))

    print(f"Generated V_out={len(tokens)} tokens -> {OUT_HEADER}")
    print(f"Generated V_in ={len(v_in)} tokens -> {IN_VOCAB_JSON}")


if __name__ == "__main__":
    build_vocab()
