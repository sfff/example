
# CXXFLAGS += -Wall -Wextra --pedantic
CXXFLAGS += -O2 --std=c++11
CXXFLAGS += -I../boost_1_55_0/
LDFLAGS += -L../boost_1_55_0/stage/lib/ -lboost_system -lboost_thread -lpthread

example:	example.cpp
		${CXX} ${CXXFLAGS} -o $@ $^ ${LDFLAGS}

