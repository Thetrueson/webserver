//web服务端程序--使用epoll模型
#include <unistd.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <signal.h>
#include <dirent.h>

#include "pub.h"
#include "wrap.h"

int http_request(int cfd, int epfd);

int main()
{
	//若web服务器给浏览器发送数据的时候, 浏览器已经关闭连接, 
	//则web服务器就会收到SIGPIPE信号
	struct sigaction act;
	act.sa_handler = SIG_IGN;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	sigaction(SIGPIPE,&act,NULL);	
	
	//改变当前进程的工作目录
	char path[255] = {0};
	sprintf(path,"%s/%s",getenv("HOME"),"webpath");
	chdir(path);
	
	//创建socket--设置端口复用---bind
	int lfd = tcp4bind(9999,NULL);
	
	//设置监听
	Listen(lfd,128);

	//创建epoll树
	int epfd = epoll_create(1024);
	//创建失败则关闭文件描述符并返回
	if(epfd<0)
	{
		perror("epoll_create error");
		close(lfd);
		return -1;
	}
	
	//将监听文件描述符lfd上树
	struct epoll_event ev;
	ev.data.fd = lfd;
	ev.events = EPOLLIN;
	epoll_ctl(epfd,EPOLL_CTL_ADD,lfd,&ev);
	
	int i;
	int cfd;
	int nready;
	int sockfd;
	struct epoll_event events[1024];
	while(1)
	{	
	
		//等待事件发生
		nready = epoll_wait(epfd,events,1024,-1);
		//失败则返回
		if(nready<0)
		{	
			//避免信号导致的正常服务器关闭
			if(errno==EINTR)
			{
				continue;
			}
			break;
		}

		for(i = 0;i<nready;i++)
		{
			sockfd = events[i].data.fd;
		
			//有客户端连接请求
			if(sockfd==lfd)
			{
			
				//接受新的客户端连接
				cfd = Accept(lfd,NULL,NULL);	
				
				//设置cfd为非阻塞
				int flag = fcntl(cfd,F_GETFL);
				flag |= O_NONBLOCK;
				fcntl(cfd,F_SETFL,flag);
				
				//将新的cfd上树
				ev.data.fd = cfd;
				ev.events = EPOLLIN;
				epoll_ctl(epfd,EPOLL_CTL_ADD,cfd,&ev);
			}
			else
			{

				//有客户端数据发来
				http_request(sockfd,epfd);

			}
		}
	}				
}

int send_header(int cfd, char *code, char *msg, char *fileType, int len)
{
	//发送消息头
	char buf[1024] = {0};
	sprintf(buf, "HTTP/1.1 %s %s\r\n", code, msg);
	sprintf(buf+strlen(buf), "Content-Type:%s\r\n", fileType);
	if(len>0)
	{
		sprintf(buf+strlen(buf), "Content-Length:%d\r\n", len);
	}
	strcat(buf, "\r\n");
	Write(cfd, buf, strlen(buf));
	return 0;
}

int send_file(int cfd, char *fileName)
{
	//打开文件
	int fd = open(fileName, O_RDONLY);
	if(fd<0)
	{
		perror("open error");
		return -1;
	}
	
	//循环读文件, 然后发送
	int n;
	char buf[1024];
	while(1)
	{
		memset(buf, 0x00, sizeof(buf));
		n = read(fd, buf, sizeof(buf));
		if(n<=0)
		{
			break;
		}
		else 
		{
			Write(cfd, buf, n);
		}
	}
}

int http_request(int cfd, int epfd)
{
	int n;
	char buf[1024];
	//读取请求行数据, 分析出要请求的资源文件名
	memset(buf, 0x00, sizeof(buf));
	n = Readline(cfd, buf, sizeof(buf));
	if(n<=0)
	{
		//printf("read error or client closed, n==[%d]\n", n);
		//关闭连接
		close(cfd);
		
		//将文件描述符从epoll树上删除
		epoll_ctl(epfd, EPOLL_CTL_DEL, cfd, NULL);
		return -1;	
	}
	printf("buf==[%s]\n", buf);
	//GET /hanzi.c HTTP/1.1
	char reqType[16] = {0};
	char fileName[255] = {0};
	char protocal[16] = {0};
	sscanf(buf, "%[^ ] %[^ ] %[^ \r\n]", reqType, fileName, protocal);
	//printf("[%s]\n", reqType);
	printf("--[%s]--\n", fileName);
	//printf("[%s]\n", protocal);
	
	char *pFile = fileName;
	if(strlen(fileName)<=1)
	{
		strcpy(pFile, "./");
	}
	else 
	{
		pFile = fileName+1;
	}
	
	//转换汉字编码
	//因为读取到的汉字为字符类型的十六进制形式
	//在console上打印会出现乱码
	strdecode(pFile, pFile);
	printf("[%s]\n", pFile);
	
	//循环读取完剩余的数据,避免产生粘包
	while((n=Readline(cfd, buf, sizeof(buf)))>0);
	
	//判断文件是否存在
	struct stat st;
	if(stat(pFile, &st)<0)
	{
		printf("file not exist\n");
		
		//发送头部信息
		send_header(cfd, "404", "NOT FOUND", get_mime_type(".html"), 0);
		
		//发送文件内容
		send_file(cfd, "error.html");	
	}
	else //若文件存在
	{
		//判断文件类型
		//普通文件
		if(S_ISREG(st.st_mode))
		{
			printf("file exist\n");
			//发送头部信息
			send_header(cfd, "200", "OK", get_mime_type(pFile), st.st_size);
			
			//发送文件内容
			send_file(cfd, pFile);
		}
		//目录文件
		else if(S_ISDIR(st.st_mode))
		{
			printf("目录文件\n");
			
			char buffer[1024];
			//发送头部信息
			send_header(cfd, "200", "OK", get_mime_type(".html"), 0);	
			
			//发送html文件头部
			send_file(cfd, "html/dir_header.html");	
			
			//文件列表信息
			struct dirent **namelist;
			int num;
			
			//读取目录信息
			num = scandir(pFile, &namelist, NULL, alphasort);
			if (num < 0)
			{
			   perror("scandir");
			   close(cfd);
			   epoll_ctl(epfd, EPOLL_CTL_DEL, cfd, NULL);
			   return -1;
			   
			}
			else 
			{
				//循环读取目录信息
			   while (num--) 
			   {
			       printf("%s\n", namelist[num]->d_name);
			       memset(buffer, 0x00, sizeof(buffer));
					//如果是目录则在超链接后加/
			       if(namelist[num]->d_type==DT_DIR)
			       {
			       		sprintf(buffer, "<li><a href=%s/>%s</a></li>", namelist[num]->d_name, namelist[num]->d_name);
			       }
			       else
			       {
			       		sprintf(buffer, "<li><a href=%s>%s</a></li>", namelist[num]->d_name, namelist[num]->d_name);
			       }
					//释放空间
			       free(namelist[num]);
			       Write(cfd, buffer, strlen(buffer));
			   }
				//最后释放空间
			   free(namelist);
			}
			//发送html尾部
			send_file(cfd, "html/dir_tail.html");		
		}
	}
	
	return 0;
}
