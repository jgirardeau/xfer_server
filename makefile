# Compiler and flags
CXXFLAGS += -O2 -g -MMD -MP -Wno-psabi
LIBS = -lbcm2835
# Directories
SRCDIR = $(CURDIR)
OBJDIR = obj
BINDIR = bin

# Source files: Automatically find all .cpp files in SRCDIR
CXX_SRCS = $(wildcard $(SRCDIR)/*.cpp)

# Object files
CXX_OBJS = $(CXX_SRCS:$(SRCDIR)/%.cpp=$(OBJDIR)/%.o)

# Dependency files
DEPS = $(CXX_OBJS:.o=.d)

# Target executable
TARGET = $(BINDIR)/uni_server

# Default target
all: $(OBJDIR) $(BINDIR) $(TARGET)

# Linking the executable
$(TARGET): $(CXX_OBJS) 
	$(CXX) $(CXX_OBJS) -o $(TARGET) $(LDFLAGS) $(LIBS)

# Compile C++ source files
$(OBJDIR)/%.o: $(SRCDIR)/%.cpp 
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Compile C source files
$(OBJDIR)/%.o: $(SRCDIR)/%.c 
	$(CC) $(CXXFLAGS) -c $< -o $@

# Create necessary directories
$(OBJDIR):
	mkdir -p $(OBJDIR)

$(BINDIR):
	mkdir -p $(BINDIR)

# Pull in auto-generated header deps
-include $(DEPS)

# Clean up build files
clean:
	rm -rf $(OBJDIR)/*.o $(TARGET)


