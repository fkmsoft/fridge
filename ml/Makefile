ML := poly --script

targets := json_test

all: $(targets)

clean:
	$(RM) $(targets) json_test.o

json_test: json.ML json_test.ML
	$(ML) compile.ML
	$(CC) -o $@ json_test.o -lpolymain -lpolyml
