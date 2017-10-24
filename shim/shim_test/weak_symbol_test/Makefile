all:
	gcc -c -fPIC -o bar_strong.o bar_strong.c
	gcc -shared -fPIC -o libbar_strong.so bar_strong.o
	gcc -g -o main1 main.c -L. -lbar_strong 
	LD_LIBRARY_PATH=. ./main1 
	gcc -c -fPIC -o bar_weak.o bar_weak.c
	gcc -shared -fPIC -o libbar_weak.so bar_weak.o
	gcc -g -o main1 main.c -L. -lbar_strong -lbar_weak
	gcc -g -o main2 main.c -L. -lbar_weak -lbar_strong
	LD_LIBRARY_PATH=. ./main1             
	LD_LIBRARY_PATH=. ./main2             
	LD_LIBRARY_PATH=. LD_PRELOAD=libbar_weak.so ./main1  
	LD_LIBRARY_PATH=. LD_PRELOAD=libbar_weak.so ./main2  
	gcc -o main3 main.c bar_strong.o bar_weak.o
	gcc -o main4 main.c bar_weak.o bar_strong.o
	./main3                                              
	./main4                                              
	gcc -DV2 -o main5 main.c -L. -lbar_strong -lbar_weak
	gcc -DV2 -o main6 main.c -L. -lbar_weak -lbar_strong
	LD_LIBRARY_PATH=. ./main5   
	LD_LIBRARY_PATH=. ./main6   
	gcc -DV2 -o main7 main.c -L. -lbar_strong -lbar_weak
	gcc -DV2 -o main8 main.c -L. -lbar_weak -lbar_strong
	LD_LIBRARY_PATH=. LD_PRELOAD=libbar_weak.so ./main7 
	LD_LIBRARY_PATH=. LD_PRELOAD=libbar_weak.so ./main8 
	gcc -DV2 -o main9  main.c -L. -lbar_strong -lbar_weak
	gcc -DV2 -o main10 main.c -L. -lbar_weak -lbar_strong
	LD_LIBRARY_PATH=. LD_PRELOAD=libbar_strong.so ./main9 
	LD_LIBRARY_PATH=. LD_PRELOAD=libbar_strong.so ./main10 
	gcc -c bar_strong.c
	gcc -c bar_weak.c
	gcc -o main11 main.c bar_strong.o bar_weak.o
	gcc -o main12 main.c bar_weak.o bar_strong.o
	./main11                                               
	./main12                                               
	gcc -o main13 -DV2 main.c bar_strong.o bar_weak.o
	gcc -o main14 -DV2 main.c bar_weak.o bar_strong.o
	./main13                                               
	./main14                                               

clean:
	rm -rf *.o *.so main[1-9]*
