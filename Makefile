NAME = queue_benchmark

CC ?= gcc
CXX ?= g++
RM ?= @rm
MKDIR ?= @mkdir

CFLAGS := -O0 -Wall -Wextra -fopenmp -g3 -DDEBUG -g
CppFLAGS := $(CFLAGS) -lstdc++

SRC_DIR = src
BUILD_DIR = build
DATA_DIR = data
INCLUDES = inc

OBJECTS = $(NAME).o queue_seq.o queue_seq_lock_global_FL.o queue_split_lock_global_FL.o


all: $(BUILD_DIR) $(NAME) $(NAME).so
	@echo "Built $(NAME)"

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

bench:
	@echo "This could run a sophisticated benchmark"

small-bench: $(BUILD_DIR) $(NAME).so $(DATA_DIR)
	@echo "Running small-bench ..."
	@python3 benchmark.py

small-plot: 
	@echo "Plotting small-bench results ..."
	bash -c 'cd plots && pdflatex "\newcommand{\DATAPATH}{../data/$$(ls ../data/ | sort -r | head -n 1)}\input{avg_plot.tex}"'
	@echo "============================================"
	@echo "Created plots/avgplot.pdf"

report: small-plot
	@echo "Compiling report ..."
	bash -c 'cd report && pdflatex report.tex'
	@echo "============================================"
	@echo "Done"

zip:
	@zip project.zip benchmark.py Makefile README src/* plots/avg_plot.tex report/report.tex run_nebula.sh

clean:
	@echo "Cleaning build directory: $(BUILD_DIR) and binaries: $(NAME) $(NAME).so"
	$(RM) -Rf $(BUILD_DIR)
	$(RM) -f $(NAME) $(NAME).so

.PHONY: clean report
