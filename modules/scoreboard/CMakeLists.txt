# abcui.scoreboard - +showscores table module (see sdk/abcui/abi.h for the
# module build contract). The interaction seat lives in the framework
# (src/framework/ScoreSeat.cpp).
abcui_add_module(scoreboard
    SOURCES
        src/ScoreboardModule.cpp)
