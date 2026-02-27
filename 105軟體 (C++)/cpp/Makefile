CXX = g++
CXXFLAGS = -std=c++17 -O3 -Wall -I./include -I/usr/include/opencv4
LDFLAGS = -lpthread -lopencv_core -lopencv_imgproc -lopencv_highgui -lopencv_videoio -lopencv_calib3d

# 如果有 Pylon，取消註解以下行
# CXXFLAGS += -DUSE_PYLON -I/opt/pylon/include
# LDFLAGS += -L/opt/pylon/lib -lpylonbase -lpylonutility -lGenApi_gcc_v3_1_Basler_pylon -lGCBase_gcc_v3_1_Basler_pylon

SRC = src/main.cpp src/IRTracker.cpp src/CameraManager.cpp
OBJ = $(SRC:.cpp=.o)
TARGET = t91_tracker

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)
