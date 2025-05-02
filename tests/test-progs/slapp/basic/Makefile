all: linear64-static linear64-dynamic linear32-static

linear64-static: linear.c dockcross-x64
	./dockcross-x64 bash -c '$$CC linear.c -o linear64-static -static'

linear64-dynamic: linear.c dockcross-x64
	./dockcross-x64 bash -c '$$CC linear.c -o linear64-dynamic'

linear32-static: linear.c dockcross-x86
	./dockcross-x86 bash -c '$$CC linear.c -o linear32-static -static'

dockcross-x64:
	docker run --rm dockcross/linux-x64 > ./dockcross-x64
	chmod +x ./dockcross-x64

dockcross-x86:
	docker run --rm dockcross/linux-x86 > ./dockcross-x86
	chmod +x ./dockcross-x86

clean:
	rm -f dockcross-* linear64-static linear64-dynamic linear32-static
