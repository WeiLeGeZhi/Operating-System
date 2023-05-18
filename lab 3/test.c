#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <time.h>
#include <string.h>

#define times 100
#define maxline (1024*1024)
#define filesize (300*1024*1024)
#define buffsize (1024*1024*1024)

char * filepathDisk[16]={"/root/lab3/read01.txt","/root/lab3/read02.txt","/root/lab3/read03.txt","/root/lab3/read04.txt","/root/lab3/read05.txt","/root/lab3/read06.txt","/root/lab3/read07.txt","/root/lab3/read08.txt","/root/lab3/read09.txt","/root/lab3/read10.txt","/root/lab3/read11.txt","/root/lab3/read12.txt","/root/lab3/read13.txt","/root/lab3/read14.txt","/root/lab3/read15.txt","/root/lab3/read16.txt"};
char * filepathRam[16]={"/root/myram/read01.txt","/root/myram/read02.txt","/root/myram/read03.txt","/root/myram/read04.txt","/root/myram/read05.txt","/root/myram/read06.txt","/root/myram/read07.txt","/root/myram/read08.txt","/root/myram/read09.txt","/root/myram/read10.txt","/root/myram/read11.txt","/root/myram/read12.txt","/root/myram/read13.txt","/root/myram/read14.txt","/root/myram/read15.txt","/root/myram/read16.txt"};
char writebuff[maxline];
char readbuff[buffsize];

void write_file(int blocksize, bool isrand, char *filepath,int fs)
{
    char *err;
	int fp=open(filepath,O_RDWR|O_CREAT|O_SYNC,0755);
    if(fp==-1) 
	{
		printf("open file error!\n");
	}
    int res;
    for(int i=0;i<times;i++)
	{
        if((res=write(fp,writebuff,blocksize))!=blocksize)
		{
            err = strerror(errno);
			printf("%d & %d & %s & %d & %s write file error!\n",blocksize,res,filepath,isrand,err);
        }
        if(isrand)
		{
            lseek(fp,rand()%fs,SEEK_SET);
        }
    }
    lseek(fp,0,SEEK_SET);
}

void read_file(int blocksize,bool isrand,char *filepath,int fs)
{
    char *err;
	int fp=open(filepath,O_RDWR|O_CREAT|O_SYNC,0755);
    if(fp==-1) 
	{
		printf("open file error!\n");
	}
    int res;
    for(int i=0;i<times;i++)
	{
        if((res=read(fp,readbuff,blocksize))!=blocksize)
		{
            err = strerror(errno);
			printf("%d & %d & %s & %d & %s read file error!\n",blocksize,res,filepath,isrand,err);
        }
        if(isrand)
		{
            lseek(fp,rand()%fs,SEEK_SET);
        }
    }
    lseek(fp,0,SEEK_SET);
}

long get_time_left(struct timeval starttime,struct timeval endtime)
{
    long spendtime;
    spendtime=(long)(endtime.tv_sec-starttime.tv_sec)*1000+(endtime.tv_usec-starttime.tv_usec)/1000;
    return spendtime;
}

int main()
{
    srand((unsigned)time(NULL));
    struct timeval ram_starttime[4],ram_endtime[4], disk_starttime[4],disk_endtime[4];
	double ram_spendtime[4],disk_spendtime[4];
	double rand_read_speed[2][6];
	double rand_write_speed[2][6];
	double seq_read_speed[2][6];
	double seq_write_speed[2][6];
	int counter = 0;
    for(int i=0;i<maxline;i=i+1)
	{
        strcat(writebuff,"a");
    }
    for(int blocksize=64;blocksize<=1024*64;blocksize=blocksize*4)
	{
        int Concurrency=7;
        int fs = filesize/Concurrency;
        //Ë³ÐòÐ´-DISKÅÌ 
        gettimeofday(&disk_starttime[0], NULL);
        for(int i=0;i<Concurrency;i++)
		{
            if(fork()==0)
			{
                write_file(blocksize,false,filepathDisk[i],fs);
                exit(0);
            }
        }
        while(wait(NULL)!=-1);
        gettimeofday(&disk_endtime[0], NULL);
        disk_spendtime[0]=get_time_left(disk_starttime[0],disk_endtime[0])/1000.0;
        seq_write_speed[0][counter]=(blocksize*Concurrency*times)/(disk_spendtime[0]*1024.0*1024.0);
        //Ë³ÐòÐ´-RAMÅÌ 
		gettimeofday(&ram_starttime[0], NULL);
        for(int i=0;i<Concurrency;i++)
		{
            if(fork()==0)
			{
                write_file(blocksize,false,filepathRam[i],fs);
                exit(0);
            }
        }
        while(wait(NULL)!=-1);
        gettimeofday(&ram_endtime[0], NULL);
        ram_spendtime[0]=get_time_left(ram_starttime[0],ram_endtime[0])/1000.0;
        seq_write_speed[1][counter]=(blocksize*Concurrency*times)/(ram_spendtime[0]*1024.0*1024.0);
        //Ëæ»úÐ´-DISKÅÌ 
        gettimeofday(&disk_starttime[1], NULL);
        for(int i=0;i<Concurrency;i++)
		{
            if(fork()==0)
			{
                write_file(blocksize,true,filepathDisk[i],fs);
                exit(0);
            }
        }
        while(wait(NULL)!=-1);
        gettimeofday(&disk_endtime[1], NULL);
        disk_spendtime[1]=get_time_left(disk_starttime[1],disk_endtime[1])/1000.0;
        rand_write_speed[0][counter]=(blocksize*Concurrency*times)/(disk_spendtime[1]*1024.0*1024.0);
        //Ëæ»úÐ´-RAMÅÌ 
		gettimeofday(&ram_starttime[1], NULL);
        for(int i=0;i<Concurrency;i++)
		{
            if(fork()==0)
			{
                write_file(blocksize,true,filepathRam[i],fs);
                exit(0);
            }
        }
        while(wait(NULL)!=-1);
        gettimeofday(&ram_endtime[1], NULL);
        ram_spendtime[1]=get_time_left(ram_starttime[1],ram_endtime[1])/1000.0;
        rand_write_speed[1][counter]=(blocksize*Concurrency*times)/(ram_spendtime[1]*1024.0*1024.0);
		//Ëæ»ú¶Á-DISKÅÌ 
		gettimeofday(&disk_starttime[2], NULL);
        for(int i=0;i<Concurrency;i++)
		{
            if(fork()==0)
			{
                read_file(blocksize,true,filepathDisk[i],fs);
                exit(0);
            }
        }
        while(wait(NULL)!=-1);
        gettimeofday(&disk_endtime[2], NULL);
        disk_spendtime[2]=get_time_left(disk_starttime[2],disk_endtime[2])/1000.0;
        rand_read_speed[0][counter]=(blocksize*Concurrency*times)/(disk_spendtime[2]*1024.0*1024.0);
        //Ëæ»ú¶Á-RAMÅÌ 
		gettimeofday(&ram_starttime[2], NULL);
        for(int i=0;i<Concurrency;i++)
		{
            if(fork()==0)
			{
                read_file(blocksize,true,filepathRam[i],fs);
                exit(0);
            }
        }
        while(wait(NULL)!=-1);
        gettimeofday(&ram_endtime[2], NULL);
        ram_spendtime[2]=get_time_left(ram_starttime[2],ram_endtime[2])/1000.0;
        rand_read_speed[1][counter]=(blocksize*Concurrency*times)/(ram_spendtime[2]*1024.0*1024.0);
        //Ë³Ðò¶Á-DISKÅÌ 
        gettimeofday(&disk_starttime[3], NULL);
        for(int i=0;i<Concurrency;i++)
		{
            if(fork()==0)
			{
                read_file(blocksize,false,filepathDisk[i],fs);
                exit(0);
            }
        }
        while(wait(NULL)!=-1);
        gettimeofday(&disk_endtime[3], NULL);
        disk_spendtime[3]=get_time_left(disk_starttime[3],disk_endtime[3])/1000.0;
        seq_read_speed[0][counter]=(blocksize*Concurrency*times)/(disk_spendtime[3]*1024.0*1024.0);
        //Ë³Ðò¶Á-RAMÅÌ 
		gettimeofday(&ram_starttime[3], NULL);
        for(int i=0;i<Concurrency;i++)
		{
            if(fork()==0)
			{
                read_file(blocksize,false,filepathRam[i],fs);
                exit(0);
            }
        }
        while(wait(NULL)!=-1);
        gettimeofday(&ram_endtime[3], NULL);
        ram_spendtime[3]=get_time_left(ram_starttime[3],ram_endtime[3])/1000.0;
        seq_read_speed[1][counter]=(blocksize*Concurrency*times)/(ram_spendtime[3]*1024.0*1024.0);
        
        counter+=1;
    }
    printf("random write:\n");
    printf("	64B	256B	1KB	4KB	16KB	64KB\n");
    printf("DISK:	%f	%f %f	%f	%f	%f\n",rand_write_speed[0][0],rand_write_speed[0][1],rand_write_speed[0][2],rand_write_speed[0][3],rand_write_speed[0][4],rand_write_speed[0][5]);
    printf("RAM:	%f	%f %f	%f	%f	%f\n",rand_write_speed[1][0],rand_write_speed[1][1],rand_write_speed[1][2],rand_write_speed[1][3],rand_write_speed[1][4],rand_write_speed[1][5]);
    printf("sequence write:\n");
    printf("	64B	256B	1KB	4KB	16KB	64KB\n");
    printf("DISK:	%f	%f %f	%f	%f	%f\n",seq_write_speed[0][0],seq_write_speed[0][1],seq_write_speed[0][2],seq_write_speed[0][3],seq_write_speed[0][4],seq_write_speed[0][5]);
    printf("RAM:	%f	%f %f	%f	%f	%f\n",seq_write_speed[1][0],seq_write_speed[1][1],seq_write_speed[1][2],seq_write_speed[1][3],seq_write_speed[1][4],seq_write_speed[1][5]);
    printf("random read:\n");
    printf("	64B	256B	1KB	4KB	16KB	64KB\n");
    printf("DISK:	%f	%f %f	%f	%f	%f\n",rand_read_speed[0][0],rand_read_speed[0][1],rand_read_speed[0][2],rand_read_speed[0][3],rand_read_speed[0][4],rand_read_speed[0][5]);
    printf("RAM:	%f	%f %f	%f	%f	%f\n",rand_read_speed[1][0],rand_read_speed[1][1],rand_read_speed[1][2],rand_read_speed[1][3],rand_read_speed[1][4],rand_read_speed[1][5]);
    printf("sequence read:\n");
    printf("	64B	256B	1KB	4KB	16KB	64KB\n");
    printf("DISK:	%f	%f %f	%f	%f	%f\n",seq_read_speed[0][0],seq_read_speed[0][1],seq_read_speed[0][2],seq_read_speed[0][3],seq_read_speed[0][4],seq_read_speed[0][5]);
    printf("RAM:	%f	%f %f	%f	%f	%f\n",seq_read_speed[1][0],seq_read_speed[1][1],seq_read_speed[1][2],seq_read_speed[1][3],seq_read_speed[1][4],seq_read_speed[1][5]);
    return 0;
}
