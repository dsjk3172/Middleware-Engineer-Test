# 미들웨어 엔지니어 기술테스트

## 들어가며
안녕하세요. 신입 개발자 이시현입니다.   
이 페이지는 AWS EC2를 이용하여 미들웨어를 구축하는 과정을 설명한 페이지입니다.   
만들어진 페이지는 [13.124.217.49](http://13.124.217.49/)에서 확인이 가능합니다.

## 1. Linux 기본 환경 구성

![image](https://github.com/dsjk3172/Open-Source-Consulting/assets/49221672/a93150c0-d4ef-4f18-85f7-a9013fe8acaf)
-----
   
![제목 없음](https://skylee22.notion.site/image/https%3A%2F%2Fprod-files-secure.s3.us-west-2.amazonaws.com%2F16110c6b-1cea-4e64-8902-8ea5643d5ee1%2F43bfc1cc-6660-4007-8a8c-32d94dfeec3f%2FUntitled.png?table=block&id=9c211ec0-9d0a-492e-a029-b0d7110475b3&spaceId=16110c6b-1cea-4e64-8902-8ea5643d5ee1&width=1910&userId=&cache=v2)

### 아마존 EC2의 경우 기본적으로 이렇게 ubuntu 22.04.3 LTS를 제공합니다.
### 저는 먼저 wasadmin이라는 계정을 만들어 보겠습니다.

- 우분투에서 wasadmin이라는 계정을 생성합니다
```
   $ adduser wasadmin
```
- 저는 지금 아마존 ec2를 사용하고 있기 때문에 이 만들어진 계정으로 접속하기 위해서는 keypair를 복사해 주어야 합니다.
```
   $ cp -r /home/ubuntu/.ssh/  /home/wasadmin/.ssh/
```
- 복사한 keypair의 권한을 ubuntu에서 wasadmin으로 변경해줍니다.
```
   $ chown -R wasadmin:wasadmin /home/wasadmin/.ssh
```
![image](https://github.com/dsjk3172/Open-Source-Consulting/assets/49221672/5e21b6c9-bc5a-4121-9012-ece3c6fafdf0)   
- wasadmin이라는 계정을 생성된 것을 확인할 수 있습니다
   
-----

## 2. JDK 환경 구성

### 이번에는 Oracle JDK를 패키지를 사용하지 않고 수동으로 설치해 보겠습니다.
### 설치는 /opt/jdk-17.0.9/ 디렉토리에 설치하도록 하겠습니다.
   
설치하기 전에 opt 폴더의 권한을 root에서 wasadmin으로 변경하도록 하겠습니다.   
이 과정은 나중에 권한 문제로 오류가 나는 걸 방지하기 위한 과정입니다.   
- root 계정의 비밀번호를 변경합니다.
```
   $ sudo passwd root
```
- root 계정으로 접속합니다
```
   $ su
```
- opt의 권한을 root에서 wasadmin으로 변경해줍니다.
```
   $ chown -R wasadmin:wasadmin /opt
```

![image](https://github.com/dsjk3172/Open-Source-Consulting/assets/49221672/fc2b2e71-4d33-426b-9bab-49a93c56a1b5)
- opt 폴더의 권한이 wasadmin으로 변경된 것을 확인할 수 있습니다.

-----
### 권한을 변경해 줬으면 다시 JDK 설치로 돌아가겠습니다.

![image](https://github.com/dsjk3172/Open-Source-Consulting/assets/49221672/026d594e-7e36-46d3-9d38-5c313310e7f1)

- 먼저 Oracle 사이트에서 자바 JDK를 다운받습니다.
- 다운받은 파일의 압축을 풉니다.
```
   $ tar zxvf (파일명)
```

![Untitled](https://skylee22.notion.site/image/https%3A%2F%2Fprod-files-secure.s3.us-west-2.amazonaws.com%2F16110c6b-1cea-4e64-8902-8ea5643d5ee1%2F756ec067-c75b-4109-801f-9394871f99f9%2FUntitled.png?table=block&id=ceee6651-a153-44d2-836f-e6244eb625f6&spaceId=16110c6b-1cea-4e64-8902-8ea5643d5ee1&width=1740&userId=&cache=v2)

- 그럼 이렇게 위 사진처럼 jdk가 설치된 것을 확인할 수 있습니다.

-----
### 다음은 환경변수를 추가하는 과정입니다.
### 이 과정을 거쳐야 리눅스 내에서 JDK를 인식하고 사용할 수 있습니다.
   
- profile을 vim을 통해 열어줍니다.
```
   $ vim /etc/profile
```

- 아래 문장들을 profile에 추가합니다.
```
JAVA_HOME=/opt/jdk-17.0.9   
JRE_HOME=/opt/jdk-17.0.9   
PATH=$PATH:$JRE_HOME/bin:$JAVA_HOME/bin   
   
export JAVA_HOME   
export JRE_HOME   
export PATH
```
   
![image](https://github.com/dsjk3172/Open-Source-Consulting/assets/49221672/162dd3cd-fb9e-4f29-85a0-c24c82f83307)

- 추가하였다면 Exc 버튼을 누른 뒤 :wq를 입력하여 vim을 종료해주세요.
- profile의 내용이 위의 사진처럼 되어야 합니다.

### 여기까지 했으면 JDK 설치는 완료되었습니다.
-----


## 3. Apache Tomcat 환경 구성(멀티 인스턴스 구성)
### 이번에는 Apache Tomcat 9.0.82를 사용하여 시스템을 구축해보도록 하겠습니다.
   
![image](https://github.com/dsjk3172/Open-Source-Consulting/assets/49221672/3c5b196b-0747-477c-a61e-c651d1a921ca)

- 우선 Tomcat을 홈페이지에서 다운받습니다.
- 다운받은 Tomcat을 opt/tomcat/폴더에 압축해제하여 주세요.
```
   $ tar -zvxf (파일명)
```
- 압축해제한 폴더명을 임의의 폴더명으로 변경합니다. 저의 경우 instance1으로 변경해 주었습니다.
```
   $ mv (기존 파일명) instance1
```
- instance1을 복사하여 instance2를 만들어줍니다.
```
   $ cp -rp /opt/tomcat/instance1 /opt/tomcat/instance2
```

- instance1

![Untitled](https://skylee22.notion.site/image/https%3A%2F%2Fprod-files-secure.s3.us-west-2.amazonaws.com%2F16110c6b-1cea-4e64-8902-8ea5643d5ee1%2F7a742d50-e6d9-4405-bf74-9f46e6aa5abe%2FUntitled.png?table=block&id=bd2e90d3-af75-46dd-a0e4-7b85946d4a8e&spaceId=16110c6b-1cea-4e64-8902-8ea5643d5ee1&width=2000&userId=&cache=v2)

- instance2

![Untitled](https://skylee22.notion.site/image/https%3A%2F%2Fprod-files-secure.s3.us-west-2.amazonaws.com%2F16110c6b-1cea-4e64-8902-8ea5643d5ee1%2F331f73c5-b277-4ac4-9457-e8539c9eb2a1%2FUntitled.png?table=block&id=c3ab33e1-317c-44de-b16a-efbbb64f881e&spaceId=16110c6b-1cea-4e64-8902-8ea5643d5ee1&width=2000&userId=&cache=v2)

- 위 사진들처럼 instance1과 instance2가 만들어졌다면 성공입니다.
-----
### 이제 멀티 인스턴스 구축을 위해 instance1과 instance2의 포트를 수정하겠습니다.

- vim으로 instance1의 server.xml 파일을 열어주세요.
```
   $ vim /opt/tomcat/instance1/conf/server.xml
```
![image](https://github.com/dsjk3172/Open-Source-Consulting/assets/49221672/c3fdbb5a-7257-4f31-8c93-3dc3869aa2c8)

- 위의 사진처럼 vim의 AJP 부분을 편집하여 AJP를 활성화 시켜주세요.

### 이번에는 instance2의 server.xml을 편집하겠습니다.

- vim으로 instance2의 server.xml 파일을 열어주세요.
```
   $ vim /opt/tomcat/instance2/conf/server.xml
```
![image](https://github.com/dsjk3172/Open-Source-Consulting/assets/49221672/d79e6672-232d-49e4-bfce-907c76356cd6)
-----
![image](https://github.com/dsjk3172/Open-Source-Consulting/assets/49221672/e8ad0aff-5aaa-49dd-b6ef-44011fc8f86c)
-----
![image](https://github.com/dsjk3172/Open-Source-Consulting/assets/49221672/96a52d77-fa72-41db-8863-e681df697014)

- instance2의 경우 instance1과의 포트 충돌을 막기 위해 위 사진들처럼 기존 포트들에 모두 1000씩 더해주세요.
-----

### 이제 인스턴트들을 실행시켜 보도록 하겠습니다.

- 인스턴트 시작
```
   $ /opt/tomcat/instance1/bin/startup.sh
   $ /opt/tomcat/instance2/bin/startup.sh
```
   
- 8080 포트로 접속한 화면
   
![image](https://github.com/dsjk3172/Open-Source-Consulting/assets/49221672/43f1e40d-3e51-4ff9-8a2a-52fcb50a1323)

- 9080 포트로 접속한 화면

![Untitled](https://skylee22.notion.site/image/https%3A%2F%2Fprod-files-secure.s3.us-west-2.amazonaws.com%2F16110c6b-1cea-4e64-8902-8ea5643d5ee1%2F94e12ae9-d5fd-4fc4-ab4f-1c6364e028c3%2FUntitled.png?table=block&id=62e40559-750b-48c3-a341-45aba229c9d2&spaceId=16110c6b-1cea-4e64-8902-8ea5643d5ee1&width=2000&userId=&cache=v2)

### 위 사진들처럼 8080, 9080포트로 접속이 성공한다면 인스턴트 구축까지 완료된 것입니다.
-----
```
- 인스턴트 시작
    $ /opt/tomcat/instance1/bin/startup.sh
    $ /opt/tomcat/instance2/bin/startup.sh
    $ /opt/apache-2.4.58/bin/httpd -k start

- 인스턴트 종료
    $ /opt/tomcat/instance1/bin/shutdown.sh
    $ /opt/tomcat/instance2/bin/shutdown.sh
    $ /opt/apache-2.4.58/bin/httpd -k stop
```
-----

## 4. 샘플 애플리케이션 배포(애플리케이션 only)
   
- 이번에는 위에서 설치한 톰캣으로 애플리케이션을 배포해보도록 하겠습니다.   
샘플 애플리케이션은 https://tomcat.apache.org/tomcat-7.0-doc/appdev/sample/ 에서   
배포하는 샘플 애플리케이션을 사용하도록 하겠습니다.
   
![image](https://github.com/dsjk3172/Open-Source-Consulting/assets/49221672/b2b3112d-9d70-444b-923b-aab4187c72b8)
- 다운받은 sample.war을 /opt/tomcat/instance1/webapps 폴더에 ROOT.war이라는 이름으로 변경하여 넣어줍니다.
   
![image](https://github.com/dsjk3172/Open-Source-Consulting/assets/49221672/5ddc15d9-0417-42a6-9ada-1d76c2a58c00)
- instacne2에도 마찬가지로 다운받은 sample.war을 /opt/tomcat/instance2/webapps 폴더에 ROOT.war이라는 이름으로 변경하여 넣어줍니다.
   
![image](https://github.com/dsjk3172/Open-Source-Consulting/assets/49221672/43f1e40d-3e51-4ff9-8a2a-52fcb50a1323)
- 8080 포트로 접속했을 때 이런 화면이 나오면 성공입니다.
-----

## 5. Apache HTTP 환경 구성(컴파일 형태로 설치)
   
### 이번에는 Apache HTTP 환경을 구성해보도록 하겠습니다.   

![Untitled](https://skylee22.notion.site/image/https%3A%2F%2Fprod-files-secure.s3.us-west-2.amazonaws.com%2F16110c6b-1cea-4e64-8902-8ea5643d5ee1%2F29ed0736-6ef6-4554-a2f3-96b24d93e175%2FUntitled.png?table=block&id=bd7c2942-b00c-4266-8597-f1912c614aba&spaceId=16110c6b-1cea-4e64-8902-8ea5643d5ee1&width=310&userId=&cache=v2)

- 우선 설치에 사용할 파일들을 다운받습니다.
   - 다운받을 파일은 Apache HTTP와 의존성 패키지인 apr, apr-util, pcre입니다
   - 저는 이 파일들을 /opt/apache_package에 다운받았습니다.

- 다운받은 파일들을 압축해제하여 주세요.
```
   $ tar -zvxf (파일명)
```

### 이제 패키지들을 하나씩 설치해봅시다.
   
- apr 설치
   - apr은 apache portable runtime의 줄임말입니다.
   - Http 서버의 핵심이며 휴대용 라이브러리로서 apr은 고급 IO기능에 대한 접근, OS수준의 기능, 기본 프로세스 처리 등의 용도로 사용됩니다.
   - apache가 설치된 OS에 관계없이 일정한 동작을 하기 위해 필요한 라이브러리입니다.
```
    $ cd /opt/apache_package/apr-1.7.4
    $ ./configure --prefix=/opt/apr
    $ make
    $ make install
```
   
- apr-util 설치
   - uitl이란 운영체계에서 제공되는 것 외에 추가의 기능을 제공하는 작은 프로그램을 의미합니다.
```
    $ cd /opt/apache_package/apr-util-1.6.3
    $ ./configure --with-apr=/opt/apr --prefix=/opt/apr-util 
    $ make
    $ make install
```
   
- PCRE 설치
   - PCRE은 Perl Compatible Regular Expressions의 줄임말입니다.
   - 펄 호환 정규 표현식으로서, 정규식 패턴 일치를 구현하는 함수의 집합입니다.
```
    $ /opt/apache_package/pcre-8.45
    $ ./configure --prefix=/opt/pcre
    $ make
    $ make install
```

- 만약 이 과정에서 문제가 발생한다면 아래를 참고해주세요.
  - error : no acceptable C compiler found in $PATH
    - 해결법 : C 컴파일러 gcc 설치
      ```
         apt-get install build-essential
      ```
  - error : xml/apr_xml.c:35:10: fatal error: expat.h : No such file or directory
    - 해결법 : C 컴파일러 gcc 설치
      ```
         apt-get install build-essential
      ```
  - error : libexpat-dev 설치(apt-get install libexpat1-dev)
    - 해결법 : apr-util 을 삭제하고 다시 설치
   
### 여기까지 진행했다면 이제 Apache HTTP 설치합니다.
   
- Apache HTTP 설치
   - --prefix=는 패키지가 설치될 경로를 지정하는 명령어입니다.
   - --with-***=은 설치한 의존성 패키지의 경로를 지정하는 명령어입니다.
```
    $ cd /opt/apache_package/httpd-2.4.58/
    $ ./configure --prefix=/opt/apache-2.4.58 \
    $ --enable-module=so --enable-rewrite --enable-so \
    $ --with-apr=/opt/apr \
    $ --with-apr-util=/opt/apr-util \
    $ --with-pcre=/opt/pcre/bin/pcre-config \
    $ --enable-mods-shared=all
    $ make
    $ make install
```

![Untitled](https://skylee22.notion.site/image/https%3A%2F%2Fprod-files-secure.s3.us-west-2.amazonaws.com%2F16110c6b-1cea-4e64-8902-8ea5643d5ee1%2F6dcf6cb3-5f94-4d45-93e0-6a4e2d2309d1%2FUntitled.png?table=block&id=57bab663-b487-45ad-88fe-548711132c6f&spaceId=16110c6b-1cea-4e64-8902-8ea5643d5ee1&width=270&userId=&cache=v2)
- 여기까지 설치가 완료되었다면 opt 폴더는 위 사진처럼 되었을 겁니다.

![image](https://github.com/dsjk3172/Open-Source-Consulting/assets/49221672/d3952855-fcbf-498d-a6d0-f698fbeca288)
- 설치가 완료된 apache-2.4.58 폴더

![Untitled](https://skylee22.notion.site/image/https%3A%2F%2Fprod-files-secure.s3.us-west-2.amazonaws.com%2F16110c6b-1cea-4e64-8902-8ea5643d5ee1%2Fc899af29-8915-45ed-88c7-c7f6f4141a38%2FUntitled.png?table=block&id=48fe848a-5d4d-43da-8e13-71d0f97bf6b2&spaceId=16110c6b-1cea-4e64-8902-8ea5643d5ee1&width=680&userId=&cache=v2)
- 페이지에 접속했을 때 이런 화면이 뜬다면 설치에 성공한 것입니다. 

## 6. Apache ↔ Tomcat 연동 환경 구성(mod_jk 연동)<이중화 Fail-over>
   
### 마지막으로 Apache와 Tomcat을 연동하고 Fail-over 환경을 구성해보도록 하겠습니다.
   
- 먼저 연동을 위한 mod_jk을 설치합니다.
- https://tomcat.apache.org/download-connectors.cgi 에서 mod_jk를 다운받은 뒤 압축을 해제해줍니다.
```
   $ tar -zvxf (파일명)
```
- 압축해제한 파일을 configure 명령어를 이용해서 설치합니다.
```
   $ cd /home/wasdamin/tomcat-connectors-1.2.49-src/native
   $ ./configure --with-apxs=/opt/apache-2.4.58/bin/apxs
   $ make && make install
```
     
![image](https://github.com/dsjk3172/Open-Source-Consulting/assets/49221672/7c7da78f-458f-44de-b010-ad4785d65a68)
- 여기서 mod_jk.so가 있다면 설치에 성공한 것입니다.
```
   $ /opt/apache-2.4.58/modules/
   $ ls
```
### 이제 설치한 mod_jk를 로드하도록 설정을 수정해보겠습니다.


![image](https://github.com/dsjk3172/Open-Source-Consulting/assets/49221672/4608a9ea-234c-481b-95f1-05fb9a9fb684)
- /opt/apache-2.4.58/conf/httpd.conf 파일을 편집합니다.
```
   $ vim /opt/apache-2.4.58/conf/httpd.conf
```
- 위 사진처럼 LoadModule jk_module modules/mod_jk.so라는 문장을 추가해주세요.
   - mod_jk.so를 Load하라는 명령입니다.
  
![image](https://github.com/dsjk3172/Open-Source-Consulting/assets/49221672/96876d08-f740-4a99-8f78-9dd29805bd44)
- 위 사진처럼 아래 문장을 추가해주세요.
   - 로드할 때 mod_jk.conf를 참고하라는 명령입니다.
```
<IfModule jk_module>
include conf/mod_jk.conf
</IfModule>
```

### 여기까지 했으면 이제 mod_jk.conf 파일을 생성해줍니다.
```
   $ vim /opt/apache-2.4.58/conf/mod_jk.conf
```
   
![image](https://github.com/dsjk3172/Open-Source-Consulting/assets/49221672/2d9e3020-1725-40d1-9d3d-0106abc2c32b)
- 위 사진처럼 문장을 추가해줍니다.
```
<IfModule mod_jk.c>
#properties 파일들을 참조하라는 명령어입니다.
JkWorkersFile conf/workers.properties
JkMountFile conf/uriworkermap.properties

#log를 생성하는 명령어입니다.
JkLogFile logs/mod_jk.log
JkShmFile logs/mod_jk.shm
JkLogLevel info
JkLogStampFormat "[%a %b %d %H:%M:%S %Y]"
</IfModule>
```

### 이제 workers.properties 파일을 생성해줍니다.
```
   $ vim /opt/apache-2.4.58/conf/workers.properties
```

![Untitled](https://skylee22.notion.site/image/https%3A%2F%2Fprod-files-secure.s3.us-west-2.amazonaws.com%2F16110c6b-1cea-4e64-8902-8ea5643d5ee1%2F96884f31-5580-4131-b94b-f8ebeecb0598%2FUntitled.png?table=block&id=f78cf22f-388e-4ff6-8d33-5391c8ee178f&spaceId=16110c6b-1cea-4e64-8902-8ea5643d5ee1&width=800&userId=&cache=v2)
- 위 사진처럼 문장을 추가해줍니다.
```
#worker.list=tomcat1, tomcat2
worker.list=balancer
ps=/

# Tomcat1
# 아래 3개는 instance 1의 server.xml과 일치하여야 합니다.
worker.tomcat1.type=ajp13
worker.tomcat1.host=13.124.217.49
worker.tomcat1.port=8009
worker.tomcat1.lbfactor=1
# lbfactor은 로드밸런싱 될 비율을 말합니다.
worker.tomcat1.secret=your_pass

# Tomcat2
# 아래 3개는 instance 2의 server.xml과 일치하여야 합니다.
worker.tomcat2.type=ajp13
worker.tomcat2.host=13.124.217.49
worker.tomcat2.port=9009
worker.tomcat2.lbfactor=1
# lbfactor은 로드밸런싱 될 비율을 말합니다.
worker.tomcat2.secret=your_pass

worker.balancer.type=lb
# 로드밸런싱을 사용한다는 명령어입니다.
worker.balancer.balance_workers=tomcat1, tomcat2
worker.balancer.sticky_session=TRUE
# 쿠키 또는 세션을 사용하여 트래픽을 분산하는 기능입니다.
```
   
### 마지막으로 uriworkermap.properties 파일을 생성해줍니다.
```
   $ vim /opt/apache-2.4.58/conf/uriworkermap.properties
```
   
![image](https://github.com/dsjk3172/Open-Source-Consulting/assets/49221672/7fe99047-9852-43eb-a5fa-0bbff360d639)
- 위 사진처럼 문장을 추가해줍니다.
- 해당 uri 를 어느 인스턴스와 맵핑할 것인지 설정하는 파일입니다.
```
/*=balancer
```

- tomcat instance1의 server.xml의 일부

![Untitled](https://skylee22.notion.site/image/https%3A%2F%2Fprod-files-secure.s3.us-west-2.amazonaws.com%2F16110c6b-1cea-4e64-8902-8ea5643d5ee1%2F47d7d1f3-a435-43d3-8bf1-34e39c63b453%2FUntitled.png?table=block&id=8bfdb5b3-e114-4990-b89b-c86e9e73496e&spaceId=16110c6b-1cea-4e64-8902-8ea5643d5ee1&width=610&userId=&cache=v2)

- tomcat instance2의 server.xml의 일부

![Untitled](https://skylee22.notion.site/image/https%3A%2F%2Fprod-files-secure.s3.us-west-2.amazonaws.com%2F16110c6b-1cea-4e64-8902-8ea5643d5ee1%2F74b88c5b-2f5c-44c4-ae5f-6d5d2d3c86da%2FUntitled.png?table=block&id=5290e9c1-daad-4898-b145-03b8eb2896fd&spaceId=16110c6b-1cea-4e64-8902-8ea5643d5ee1&width=530&userId=&cache=v2)
-----

연동 전 화면 :

![Untitled](https://skylee22.notion.site/image/https%3A%2F%2Fprod-files-secure.s3.us-west-2.amazonaws.com%2F16110c6b-1cea-4e64-8902-8ea5643d5ee1%2Fc899af29-8915-45ed-88c7-c7f6f4141a38%2FUntitled.png?table=block&id=af34523b-f779-4d5d-9b21-e15e1b4917ec&spaceId=16110c6b-1cea-4e64-8902-8ea5643d5ee1&width=680&userId=&cache=v2)
-----

연동 후 화면:

![Untitled](https://skylee22.notion.site/image/https%3A%2F%2Fprod-files-secure.s3.us-west-2.amazonaws.com%2F16110c6b-1cea-4e64-8902-8ea5643d5ee1%2Fd05b42bb-bf80-4257-be95-a66fde76b7d2%2FUntitled.png?table=block&id=4fc23672-fb77-44c9-a04d-d9d9c5b2a194&spaceId=16110c6b-1cea-4e64-8902-8ea5643d5ee1&width=2000&userId=&cache=v2)

- mod_jk 방식으로 Apache-Tomcat 상호연동
- mod_jk 방식으로 이중화 구성
- 로드밸런싱을 이용하여 두 개의 인스턴스 중 하나가 죽더라도 다른 하나가 실행중이라면 계속해서 실행됨
