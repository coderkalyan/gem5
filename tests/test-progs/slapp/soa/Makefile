all: aos64-static aos64-dynamic aos32-static soa64-static soa64-dynamic soa32-static

aos64-static: aos.c dockcross-x64
	./dockcross-x64 bash -c '$$CC aos.c -o aos64-static -static'

aos64-dynamic: aos.c dockcross-x64
	./dockcross-x64 bash -c '$$CC aos.c -o aos64-dynamic'

aos32-static: aos.c dockcross-x86
	./dockcross-x86 bash -c '$$CC aos.c -o aos32-static -static'

soa64-static: soa.c dockcross-x64
	./dockcross-x64 bash -c '$$CC soa.c -o soa64-static -static'

soa64-dynamic: soa.c dockcross-x64
	./dockcross-x64 bash -c '$$CC soa.c -o soa64-dynamic'

soa32-static: soa.c dockcross-x86
	./dockcross-x86 bash -c '$$CC soa.c -o soa32-static -static'


dockcross-x64:
	docker run --rm dockcross/linux-x64 > ./dockcross-x64
	chmod +x ./dockcross-x64

dockcross-x86:
	docker run --rm dockcross/linux-x86 > ./dockcross-x86
	chmod +x ./dockcross-x86

clean:
	rm -f dockcross-* aos64-static aos64-dynamic aos32-static soa-64-static soa64-dynamic soa32-static
