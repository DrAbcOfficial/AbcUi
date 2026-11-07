# abcui.chat - chat log + input line module (see sdk/abcui/abi.h for the
# module build contract).
abcui_add_module(chat
    SOURCES
        src/ChatModule.cpp
        src/saytext/SayText.cpp)
