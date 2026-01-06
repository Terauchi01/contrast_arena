CXX := g++
CXXFLAGS := -std=c++20 -Wall -Wextra -pedantic -O2
INCLUDES := -Icommon -Icore/include -Iai/include

SERVER_SRC := server/main.cpp
CLIENT_SRC := client/main.cpp
CORE_SRCS := $(wildcard core/src/*.cpp)
POLICY_SRCS := ai/src/rule_based_policy.cpp \
	ai/src/rule_based_policy2.cpp \
	ai/src/random_policy.cpp \
	ai/src/ntuple_big.cpp \
	ai/src/alphabeta.cpp \
	ai/src/mcts.cpp
TEST_SRCS := ai/main.cpp \
	$(POLICY_SRCS)

all: server client test_rulebased

server: $(SERVER_SRC) $(CORE_SRCS) common/protocol.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SERVER_SRC) $(CORE_SRCS) -o server_app

client: $(CLIENT_SRC) $(CORE_SRCS) $(POLICY_SRCS) common/protocol.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(CLIENT_SRC) $(CORE_SRCS) $(POLICY_SRCS) -o client_app

test_rulebased: $(TEST_SRCS) $(CORE_SRCS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(TEST_SRCS) $(CORE_SRCS) -o test_rulebased_app

ai_test: test_rulebased

clean:
	rm -f server_app client_app test_rulebased_app

.PHONY: all server client test_rulebased clean
