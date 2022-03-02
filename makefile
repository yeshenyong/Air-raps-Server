CXX ?= g++
# ?= 是如果没有被赋值过就赋予等号后面的值
DEBUG ?= 1
ifeq ($(DEBUG), 1)
	CXXFLAGS += -g
	CXXFLAGS += -std=c++11
else
	CXXFLAGS += -02

endif

server: main.cpp ./timer/lst_timer.cpp ./http/http_con.cpp ./log/log.cpp ./CGImysql/sql_connection_pool.cpp ./webServer/webserver.cpp ./config/config.cpp
		$(CXX) -o server $^ $(CXXFLAGS) -lpthread -lmysqlclient -lcryptopp

clean:
	rm -r server