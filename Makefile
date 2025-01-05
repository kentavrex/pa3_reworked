all: pa4

env:
	export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:./lib64

send_to_vm:
	git add . && git commit -m "after 40%" && git push origin main

build:
	git pull && clang -std=c99 -Wall -pedantic *.c -Llib64 -lruntime -o pa_program

run:
	./pa_program -p 3 10 50 80

clear:
	rm pa_program && clear && ls -l