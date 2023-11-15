Open-Source-Consulting   
   


1. Linux 기본 환경 구성   
   
   
![제목 없음]([https://user-images.githubusercontent.com/49221672/113817268-0b541f00-97b1-11eb-911a-dc4a2616e147.png](https://prod-files-secure.s3.us-west-2.amazonaws.com/16110c6b-1cea-4e64-8902-8ea5643d5ee1/43bfc1cc-6660-4007-8a8c-32d94dfeec3f/Untitled.png](https://skylee22.notion.site/image/https%3A%2F%2Fprod-files-secure.s3.us-west-2.amazonaws.com%2F16110c6b-1cea-4e64-8902-8ea5643d5ee1%2F43bfc1cc-6660-4007-8a8c-32d94dfeec3f%2FUntitled.png?table=block&id=9c211ec0-9d0a-492e-a029-b0d7110475b3&spaceId=16110c6b-1cea-4e64-8902-8ea5643d5ee1&width=1910&userId=&cache=v2))

          

- 환경: AWS EC2, ubuntu 22.04.3 LTS 사용
- wasadmin 계정 사용
- ip : [13.124.217.49:](http://13.124.217.49/)(현재 접속 가능)

1. JDK 환경 구성

![Untitled](https://prod-files-secure.s3.us-west-2.amazonaws.com/16110c6b-1cea-4e64-8902-8ea5643d5ee1/756ec067-c75b-4109-801f-9394871f99f9/Untitled.png)

- Oracle JDK 17.0.9 버전 설치(패키지 설치 X)
- Directory: /opt/jdk-17.0.9/

1. Apache Tomcat 환경 구성(멀티 인스턴스 구축)
- instance1

![Untitled](https://prod-files-secure.s3.us-west-2.amazonaws.com/16110c6b-1cea-4e64-8902-8ea5643d5ee1/7a742d50-e6d9-4405-bf74-9f46e6aa5abe/Untitled.png)

- instance2

![Untitled](https://prod-files-secure.s3.us-west-2.amazonaws.com/16110c6b-1cea-4e64-8902-8ea5643d5ee1/331f73c5-b277-4ac4-9457-e8539c9eb2a1/Untitled.png)

- 8080 포트로 접속한 화면

![Untitled](https://prod-files-secure.s3.us-west-2.amazonaws.com/16110c6b-1cea-4e64-8902-8ea5643d5ee1/19916bde-a2b6-4f54-9a17-f3e2209fddde/Untitled.png)

- 9080 포트로 접속한 화면

![Untitled](https://prod-files-secure.s3.us-west-2.amazonaws.com/16110c6b-1cea-4e64-8902-8ea5643d5ee1/94e12ae9-d5fd-4fc4-ab4f-1c6364e028c3/Untitled.png)

- Apache Tomcat 9.0.82를 사용하여 시스템 구축
- Instance1 Directory : /opt/tomcat/instance1/
- Instance2 Directory : /opt/tomcat/instance2/
- Instance1은 HTTP 포트 8080, AJP 포트 8009로 구성
- Instance2는 HTTP 포트 9080, AJP 포트 9009로 구성

- 인스턴트 시작
    - /opt/tomcat/instance1/bin/startup.sh
    - /opt/tomcat/instance2/bin/startup.sh
    - /opt/apache-2.4.58/bin/httpd -k start

- 인스턴트 종료
    - /opt/tomcat/instance1/bin/shutdown.sh
    - /opt/tomcat/instance2/bin/shutdown.sh
    - /opt/apache-2.4.58/bin/httpd -k stop

1. 샘플 애플리케이션 배포(애플리케이션 only)

![Untitled](https://prod-files-secure.s3.us-west-2.amazonaws.com/16110c6b-1cea-4e64-8902-8ea5643d5ee1/86d1edca-7741-46d2-adad-42b24861154d/Untitled.png)

- https://tomcat.apache.org/tomcat-7.0-doc/appdev/sample/에서 배포하는 샘플 애플리케이션을 사용

1. Apache HTTP 환경 구성(컴파일 형태로 설치)
- apache-2.4.58 폴더

![Untitled](https://prod-files-secure.s3.us-west-2.amazonaws.com/16110c6b-1cea-4e64-8902-8ea5643d5ee1/46aa6b43-5450-4ae4-8134-ba9b6ccf9015/Untitled.png)

- opt 폴더

![Untitled](https://prod-files-secure.s3.us-west-2.amazonaws.com/16110c6b-1cea-4e64-8902-8ea5643d5ee1/6dcf6cb3-5f94-4d45-93e0-6a4e2d2309d1/Untitled.png)

- 설치에 사용한 파일들

![Untitled](https://prod-files-secure.s3.us-west-2.amazonaws.com/16110c6b-1cea-4e64-8902-8ea5643d5ee1/29ed0736-6ef6-4554-a2f3-96b24d93e175/Untitled.png)

- Apache HTTP 환경 구성 결과

![Untitled](https://prod-files-secure.s3.us-west-2.amazonaws.com/16110c6b-1cea-4e64-8902-8ea5643d5ee1/c899af29-8915-45ed-88c7-c7f6f4141a38/Untitled.png)

- Apache 2.4.58 version 설치
- APR, PCRE와 같은 의존성 패키지는 /opt/에 설치
- Directory : /opt/apache-2.4.58

1. Apache ↔ Tomcat 연동 환경 구성(mod_jk 연동)<이중화 Fail-over>
- 설치된 모듈들

![Untitled](https://prod-files-secure.s3.us-west-2.amazonaws.com/16110c6b-1cea-4e64-8902-8ea5643d5ee1/a09ca6a3-a3d8-4a7c-aa8a-fcab9a739794/Untitled.png)

- tomcat instance1의 server.xml의 일부

![Untitled](https://prod-files-secure.s3.us-west-2.amazonaws.com/16110c6b-1cea-4e64-8902-8ea5643d5ee1/47d7d1f3-a435-43d3-8bf1-34e39c63b453/Untitled.png)

- tomcat instance2의 server.xml의 일부

![Untitled](https://prod-files-secure.s3.us-west-2.amazonaws.com/16110c6b-1cea-4e64-8902-8ea5643d5ee1/74b88c5b-2f5c-44c4-ae5f-6d5d2d3c86da/Untitled.png)

- workers.properties

![Untitled](https://prod-files-secure.s3.us-west-2.amazonaws.com/16110c6b-1cea-4e64-8902-8ea5643d5ee1/96884f31-5580-4131-b94b-f8ebeecb0598/Untitled.png)

연동 전 화면 :

![Untitled](https://prod-files-secure.s3.us-west-2.amazonaws.com/16110c6b-1cea-4e64-8902-8ea5643d5ee1/c899af29-8915-45ed-88c7-c7f6f4141a38/Untitled.png)

연동 후 화면:

![Untitled](https://prod-files-secure.s3.us-west-2.amazonaws.com/16110c6b-1cea-4e64-8902-8ea5643d5ee1/d05b42bb-bf80-4257-be95-a66fde76b7d2/Untitled.png)

- mod_jk 방식으로 Apache-Tomcat 상호연동
- mod_jk 방식으로 이중화 구성
- 로드밸런싱을 이용하여 두 개의 인스턴스 중 하나가 죽더라도 다른 하나가 실행중이라면 계속해서 실행됨

https://honeywater97.tistory.com/94

https://ta-starter.tistory.com/14

https://osc131.tistory.com/38

https://yooloo.tistory.com/171

[13.124.217.49](http://13.124.217.49/)

/opt/tomcat/instance1/bin/startup.sh

/opt/tomcat/instance2/bin/startup.sh

/opt/tomcat/instance1/bin/shutdown.sh

/opt/tomcat/instance2/bin/shutdown.sh

/opt/apache-2.4.58/bin/httpd -k start

/opt/apache-2.4.58/bin/httpd -k stop

vim /opt/tomcat/instance1/conf/server.xml

vim /opt/tomcat/instance1/conf/server.xml

vim /opt/apache-2.4.58/conf/
