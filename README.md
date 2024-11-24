publisher.c: Contains the code for publishing the data to broker
broker.c: Contains the code for accepting the data to from publisher & sending the data to subscriber based on what topics the subscribers have subscribed
subscriber.c: Contains the code for getting the data from broker for subscribers from the respective topics they have subscribed to
getdata.c: Fetches the news data from API & stores it in file news_articles.json

Install the following dependencies beforehand:
sudo apt install libcjson-dev
sudo apt install libcurl4-openssl-dev

Steps to run the project:
1. Download the repository
2. Get inside the project directory
3. make
4. ./broker
5. ./subscriber
6. ./publisher
