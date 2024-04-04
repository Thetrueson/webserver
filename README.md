# webserver
c语言在linux下基于epoll模型实现的webserver，使用到了第三方的封装库，源码进行了批注，供参考和学习使用

这个版本为简单的多路IO复用思路实现，可以在此基础上改进为多线程多进程

使用方法：
  因为源码是将默认目录设为/用户（当前登录）/
  所以把所有文件放在这个目录下
  用gcc -o webserver webserver.c wrap.c pub.c编译代码
  之后用./webserver运行即可
