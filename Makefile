NAME = queue_benchmark
TEST_NAME = test_queue

VENV := .venv
PYTHON := $(VENV)/bin/python
PIP := $(VENV)/bin/pip
REQUIREMENTS := requirements.txt

CC ?= gcc
CXX ?= g++
RM ?= @rm
MKDIR ?= @mkdir

FLAGS := -O3 -Wall -Wextra -fopenmp -g3 -DDEBUG -g
CFLAGS := $(FLAGS)
CppFLAGS := $(FLAGS) -lstdc++

SRC_DIR = src
BUILD_DIR = build
DATA_DIR = data
INCLUDES = inc

QUEUE_OBJECTS = queue_seq.o queue_seq_lock_global_FL.o queue_split_lock_global_FL.o queue_lock_free_local_FL.o  thread_stats_tls.o  queue_split_lock_local_FL.o queue_seq_lock_local_FL.o  concurrent_bag.o concurrent_bag_factory.o
OBJECTS = $(NAME).o $(QUEUE_OBJECTS) 
TEST_OBJECTS = $(TEST_NAME).o $(QUEUE_OBJECTS)


$(VENV):
	@echo "Creating Python virtual environment..."
	python3 -m venv $(VENV)
	$(PIP) install --upgrade pip

deps: $(VENV)
	@echo "Installing Python dependencies into virtualenv..."
	$(PIP) install -r $(REQUIREMENTS)

all: $(BUILD_DIR) $(NAME) $(NAME).so
	@echo "Built $(NAME) from $(OBJECTS)"

$(DATA_DIR):
	@echo "Creating data directory: $(DATA_DIR)"
	$(MKDIR) $(DATA_DIR)

$(BUILD_DIR):
	@echo "Creating build directory: $(BUILD_DIR)"
	$(MKDIR) $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@echo "Compiling $<"
	$(CC) $(CFLAGS) -fPIC -I$(INCLUDES) -c -o $@ $<

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@echo "Compiling $<"
	$(CXX) $(CppFLAGS) -fPIC -I$(INCLUDES) -c -o $@ $<

$(NAME): $(foreach object,$(OBJECTS),$(BUILD_DIR)/$(object))
	@echo "Linking $(NAME)"
	$(CXX) $(CFLAGS) -o $@ $^

$(NAME).so: $(foreach object,$(OBJECTS),$(BUILD_DIR)/$(object))
	@echo "Linking $(NAME)"
	$(CXX) $(CFLAGS) -fPIC -shared -o $@ $^ 

test: $(BUILD_DIR) $(TEST_NAME) $(TEST_NAME).so
	@echo "Build $(TEST_NAME)"

$(TEST_NAME): $(foreach object,$(TEST_OBJECTS),$(BUILD_DIR)/$(object))
	@echo "Linking $(NAME)"
	$(CXX) $(CFLAGS) -o $@ $^

$(TEST_NAME).so: $(foreach object,$(TEST_OBJECTS),$(BUILD_DIR)/$(object))
	@echo "Linking $(NAME)"
	$(CXX) $(CFLAGS) -fPIC -shared -o $@ $^ 

bench: $(BUILD_DIR) $(NAME).so $(DATA_DIR)
	@echo "Running full benchmark ..."
	@python3 benchmark.py

small-bench: $(BUILD_DIR) $(NAME).so $(DATA_DIR)
	@echo "Running small-bench ..."
	@python3 benchmark.py -s -c
	
small-plot: deps
	@echo "Generating plots using Python..."
	$(PYTHON) plot.py
	@echo "Plots written to ./plot/"

report: small-plot
	@echo "Compiling report ..."
	bash -c 'cd report && pdflatex report.tex'
	@echo "============================================"
	@echo "Created report/report.pdf"

zip:
	@echo "Creating submission archive..."
	@zip -r project.zip \
		benchmark.py \
		plot.py \
		requirements.txt \
		Makefile \
		README \
		src/ \
		plot/ \
		report/report.tex \
		run_nebula.sh \
		-x .venv/*

clean:
	@echo "Cleaning build directory: $(BUILD_DIR) and binaries: $(NAME) $(NAME).so"
	$(RM) -Rf $(BUILD_DIR)
	$(RM) -f $(NAME) $(NAME).so
	$(RM) -f $(TEST_NAME).so

.PHONY: clean report
