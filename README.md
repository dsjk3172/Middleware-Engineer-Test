# 오픈소스컨설팅 미들웨어 엔지니어 기술테스트

1. Linux 기본 환경 구성
   
![image](https://github.com/dsjk3172/Open-Source-Consulting/assets/49221672/a93150c0-d4ef-4f18-85f7-a9013fe8acaf)
-----

![제목 없음](https://skylee22.notion.site/image/https%3A%2F%2Fprod-files-secure.s3.us-west-2.amazonaws.com%2F16110c6b-1cea-4e64-8902-8ea5643d5ee1%2F43bfc1cc-6660-4007-8a8c-32d94dfeec3f%2FUntitled.png?table=block&id=9c211ec0-9d0a-492e-a029-b0d7110475b3&spaceId=16110c6b-1cea-4e64-8902-8ea5643d5ee1&width=1910&userId=&cache=v2)
    
- 환경: AWS EC2, ubuntu 22.04.3 LTS 사용
- wasadmin 계정 사용
- SSH 접속 터미널: MobaXterm
- ip : [13.124.217.49](http://13.124.217.49/)(현재 접속 가능)
-----

- 문제점 : wasadmin 계정 생성시 접속 문제 발생
- 해결 : 같은 keypair로 로그인 할 수 있도록 ubuntu 의 ./ssh 디렉토리를 wasdamin 계정으로 복사하고 권한 부여
    - cp -r /home/ubuntu/.ssh/  /home/wasadmin/.ssh/   
    - chown -R wasadmin:wasadmin /home/wasadmin/.ssh   
-----

2. JDK 환경 구성

![Untitled](https://skylee22.notion.site/image/https%3A%2F%2Fprod-files-secure.s3.us-west-2.amazonaws.com%2F16110c6b-1cea-4e64-8902-8ea5643d5ee1%2F756ec067-c75b-4109-801f-9394871f99f9%2FUntitled.png?table=block&id=ceee6651-a153-44d2-836f-e6244eb625f6&spaceId=16110c6b-1cea-4e64-8902-8ea5643d5ee1&width=1740&userId=&cache=v2)

- Oracle JDK 17.0.9 버전 설치(패키지 설치 X)
- Directory: /opt/jdk-17.0.9/
-----
![image](https://github.com/dsjk3172/Open-Source-Consulting/assets/49221672/162dd3cd-fb9e-4f29-85a0-c24c82f83307)
   
- /etc/profile에 환경변수 추가
-----


3. Apache Tomcat 환경 구성(멀티 인스턴스 구축)   
- Apache Tomcat 9.0.82를 사용하여 시스템 구축
-----
- instance1

![Untitled](https://skylee22.notion.site/image/https%3A%2F%2Fprod-files-secure.s3.us-west-2.amazonaws.com%2F16110c6b-1cea-4e64-8902-8ea5643d5ee1%2F7a742d50-e6d9-4405-bf74-9f46e6aa5abe%2FUntitled.png?table=block&id=bd2e90d3-af75-46dd-a0e4-7b85946d4a8e&spaceId=16110c6b-1cea-4e64-8902-8ea5643d5ee1&width=2000&userId=&cache=v2)

- instance2

![Untitled](https://skylee22.notion.site/image/https%3A%2F%2Fprod-files-secure.s3.us-west-2.amazonaws.com%2F16110c6b-1cea-4e64-8902-8ea5643d5ee1%2F331f73c5-b277-4ac4-9457-e8539c9eb2a1%2FUntitled.png?table=block&id=c3ab33e1-317c-44de-b16a-efbbb64f881e&spaceId=16110c6b-1cea-4e64-8902-8ea5643d5ee1&width=2000&userId=&cache=v2)

- Instance1 Directory : /opt/tomcat/instance1/
- Instance2 Directory : /opt/tomcat/instance2/
-----
- 8080 포트로 접속한 화면

![image](https://github.com/dsjk3172/Open-Source-Consulting/assets/49221672/43f1e40d-3e51-4ff9-8a2a-52fcb50a1323)

- 9080 포트로 접속한 화면

![Untitled](https://skylee22.notion.site/image/https%3A%2F%2Fprod-files-secure.s3.us-west-2.amazonaws.com%2F16110c6b-1cea-4e64-8902-8ea5643d5ee1%2F94e12ae9-d5fd-4fc4-ab4f-1c6364e028c3%2FUntitled.png?table=block&id=62e40559-750b-48c3-a341-45aba229c9d2&spaceId=16110c6b-1cea-4e64-8902-8ea5643d5ee1&width=2000&userId=&cache=v2)

- Instance1은 HTTP 포트 8080, AJP 포트 8009로 구성
- Instance2는 HTTP 포트 9080, AJP 포트 9009로 구성
-----

- 인스턴트 시작
    - /opt/tomcat/instance1/bin/startup.sh
    - /opt/tomcat/instance2/bin/startup.sh
    - /opt/apache-2.4.58/bin/httpd -k start

- 인스턴트 종료
    - /opt/tomcat/instance1/bin/shutdown.sh
    - /opt/tomcat/instance2/bin/shutdown.sh
    - /opt/apache-2.4.58/bin/httpd -k stop
-----

4. 샘플 애플리케이션 배포(애플리케이션 only)

![Untitled](https://skylee22.notion.site/image/https%3A%2F%2Fprod-files-secure.s3.us-west-2.amazonaws.com%2F16110c6b-1cea-4e64-8902-8ea5643d5ee1%2F86d1edca-7741-46d2-adad-42b24861154d%2FUntitled.png?table=block&id=d238b6db-d1ff-44a9-9c4e-6f4a4e6116c6&spaceId=16110c6b-1cea-4e64-8902-8ea5643d5ee1&width=2000&userId=&cache=v2)

- https://tomcat.apache.org/tomcat-7.0-doc/appdev/sample/ 에서 배포하는 샘플 애플리케이션을 사용
-----
![image](https://github.com/dsjk3172/Open-Source-Consulting/assets/49221672/b2b3112d-9d70-444b-923b-aab4187c72b8)
- The easiest way to run this application is simply to move the war file to your CATALINA_HOME/webapps directory
-----

5. Apache HTTP 환경 구성(컴파일 형태로 설치)
- apache-2.4.58 폴더

![Untitled](https://skylee22.notion.site/image/https%3A%2F%2Fprod-files-secure.s3.us-west-2.amazonaws.com%2F16110c6b-1cea-4e64-8902-8ea5643d5ee1%2F46aa6b43-5450-4ae4-8134-ba9b6ccf9015%2FUntitled.png?table=block&id=deff3fa2-2401-48a5-a8e3-2afcc7095084&spaceId=16110c6b-1cea-4e64-8902-8ea5643d5ee1&width=1950&userId=&cache=v2)

- opt 폴더

![Untitled](https://skylee22.notion.site/image/https%3A%2F%2Fprod-files-secure.s3.us-west-2.amazonaws.com%2F16110c6b-1cea-4e64-8902-8ea5643d5ee1%2F6dcf6cb3-5f94-4d45-93e0-6a4e2d2309d1%2FUntitled.png?table=block&id=57bab663-b487-45ad-88fe-548711132c6f&spaceId=16110c6b-1cea-4e64-8902-8ea5643d5ee1&width=270&userId=&cache=v2)

- 설치에 사용한 파일들

![Untitled](https://skylee22.notion.site/image/https%3A%2F%2Fprod-files-secure.s3.us-west-2.amazonaws.com%2F16110c6b-1cea-4e64-8902-8ea5643d5ee1%2F29ed0736-6ef6-4554-a2f3-96b24d93e175%2FUntitled.png?table=block&id=bd7c2942-b00c-4266-8597-f1912c614aba&spaceId=16110c6b-1cea-4e64-8902-8ea5643d5ee1&width=310&userId=&cache=v2)

- Apache HTTP 환경 구성 결과

![Untitled](https://skylee22.notion.site/image/https%3A%2F%2Fprod-files-secure.s3.us-west-2.amazonaws.com%2F16110c6b-1cea-4e64-8902-8ea5643d5ee1%2Fc899af29-8915-45ed-88c7-c7f6f4141a38%2FUntitled.png?table=block&id=48fe848a-5d4d-43da-8e13-71d0f97bf6b2&spaceId=16110c6b-1cea-4e64-8902-8ea5643d5ee1&width=680&userId=&cache=v2)

- Apache 2.4.58 version 설치
- APR, PCRE와 같은 의존성 패키지는 /opt/에 설치
- Directory : /opt/apache-2.4.58
-----
- 설치 과정에서 발생한 문제들
   1. error : no acceptable C compiler found in $PATH
      - 해결법 : C 컴파일러 gcc 설치(sudo apt-get install build-essential)
   
   2. error : xml/apr_xml.c:35:10: fatal error: expat.h : No such file or directory
      - 해결법 : libexpat-dev 설치(apt-get install libexpat1-dev)
   
   3. error : Makefile:48: recipe for target 'htpasswd' failed
      - 해결법 : apr-util 을 삭제하고 다시 설치

-----

6. Apache ↔ Tomcat 연동 환경 구성(mod_jk 연동)<이중화 Fail-over>
- 설치된 모듈들

![Untitled](https://skylee22.notion.site/image/https%3A%2F%2Fprod-files-secure.s3.us-west-2.amazonaws.com%2F16110c6b-1cea-4e64-8902-8ea5643d5ee1%2Fa09ca6a3-a3d8-4a7c-aa8a-fcab9a739794%2FUntitled.png?table=block&id=0c6fddef-cb81-4b3a-8cf1-c8784ac2ace4&spaceId=16110c6b-1cea-4e64-8902-8ea5643d5ee1&width=1250&userId=&cache=v2)

- tomcat instance1의 server.xml의 일부

![Untitled](https://skylee22.notion.site/image/https%3A%2F%2Fprod-files-secure.s3.us-west-2.amazonaws.com%2F16110c6b-1cea-4e64-8902-8ea5643d5ee1%2F47d7d1f3-a435-43d3-8bf1-34e39c63b453%2FUntitled.png?table=block&id=8bfdb5b3-e114-4990-b89b-c86e9e73496e&spaceId=16110c6b-1cea-4e64-8902-8ea5643d5ee1&width=610&userId=&cache=v2)

- tomcat instance2의 server.xml의 일부

![Untitled](https://skylee22.notion.site/image/https%3A%2F%2Fprod-files-secure.s3.us-west-2.amazonaws.com%2F16110c6b-1cea-4e64-8902-8ea5643d5ee1%2F74b88c5b-2f5c-44c4-ae5f-6d5d2d3c86da%2FUntitled.png?table=block&id=5290e9c1-daad-4898-b145-03b8eb2896fd&spaceId=16110c6b-1cea-4e64-8902-8ea5643d5ee1&width=530&userId=&cache=v2)

- workers.properties

![Untitled](https://skylee22.notion.site/image/https%3A%2F%2Fprod-files-secure.s3.us-west-2.amazonaws.com%2F16110c6b-1cea-4e64-8902-8ea5643d5ee1%2F96884f31-5580-4131-b94b-f8ebeecb0598%2FUntitled.png?table=block&id=f78cf22f-388e-4ff6-8d33-5391c8ee178f&spaceId=16110c6b-1cea-4e64-8902-8ea5643d5ee1&width=800&userId=&cache=v2)

연동 전 화면 :

![Untitled](https://skylee22.notion.site/image/https%3A%2F%2Fprod-files-secure.s3.us-west-2.amazonaws.com%2F16110c6b-1cea-4e64-8902-8ea5643d5ee1%2Fc899af29-8915-45ed-88c7-c7f6f4141a38%2FUntitled.png?table=block&id=af34523b-f779-4d5d-9b21-e15e1b4917ec&spaceId=16110c6b-1cea-4e64-8902-8ea5643d5ee1&width=680&userId=&cache=v2)
-----

연동 후 화면:

![Untitled](https://skylee22.notion.site/image/https%3A%2F%2Fprod-files-secure.s3.us-west-2.amazonaws.com%2F16110c6b-1cea-4e64-8902-8ea5643d5ee1%2Fd05b42bb-bf80-4257-be95-a66fde76b7d2%2FUntitled.png?table=block&id=4fc23672-fb77-44c9-a04d-d9d9c5b2a194&spaceId=16110c6b-1cea-4e64-8902-8ea5643d5ee1&width=2000&userId=&cache=v2)

- mod_jk 방식으로 Apache-Tomcat 상호연동
- mod_jk 방식으로 이중화 구성
- 로드밸런싱을 이용하여 두 개의 인스턴스 중 하나가 죽더라도 다른 하나가 실행중이라면 계속해서 실행됨
