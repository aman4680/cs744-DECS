#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <cjson/cJSON.h>

#define MAX_TOPICS 10
#define MAX_BUFFER_SIZE 8192
#define PORT_SUBSCRIBER 8080
#define NO_OF_SUBSCRIBERS 3

// Data structure for a subscriber
typedef struct
{
    int sockfd;
    char *topics[MAX_TOPICS];
    int topic_count;
} Subscriber;

// Function to handle incoming data (news articles) from the broker
void handle_received_data(int sockfd)
{
    char buffer[MAX_BUFFER_SIZE];
    int bytes_received;

    while (1)
    {
        // Receive data from the broker
        bytes_received = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received <= 0)
        {
            printf("Disconnected from broker or error in receiving data.\n");
            break;
        }

        buffer[bytes_received] = '\0'; // Null-terminate the received data
        printf("Received data from broker: %s\n", buffer);

        // Parse the received JSON data (article)
        cJSON *root = cJSON_Parse(buffer);
        if (root == NULL)
        {
            printf("Error parsing JSON data.\n");
            continue;
        }

        // Assuming the data has a "source" object with "name" as the topic name
        cJSON *source = cJSON_GetObjectItem(root, "source");
        if (source == NULL)
        {
            printf("No source field found in the article.\n");
            cJSON_Delete(root);
            continue;
        }

        cJSON *name = cJSON_GetObjectItem(source, "name");
        if (name == NULL)
        {
            printf("No name field found in the source.\n");
            cJSON_Delete(root);
            continue;
        }

        // Display the article's title, description, and URL
        cJSON *title = cJSON_GetObjectItem(root, "title");
        cJSON *description = cJSON_GetObjectItem(root, "description");
        cJSON *url = cJSON_GetObjectItem(root, "url");
        if (title && description && url)
        {
            printf("\nNew Article Received (Topic: %s)\n", name->valuestring);
            printf("Title: %s\n", title->valuestring);
            printf("Description: %s\n", description->valuestring);
            printf("URL: %s\n", url->valuestring);
        }
        else
        {
            printf("Incomplete article data.\n");
        }

        cJSON_Delete(root); // Don't forget to free the cJSON object after use
    }
}

// Function to send subscription request to the broker
// void send_subscription_request(int sockfd, const char *topics)
// {
//     send(sockfd, topics, strlen(topics), 0);
//     printf("Sent subscription request: %s\n", topics);
// }

// Function to handle the subscriber's connection
void *handle_subscriber(void *arg)
{
    Subscriber *subscriber = (Subscriber *)arg;
    char buffer[1024];
    int bytes_received;

    // Step 1: Send subscription information to the broker
    // Start with an empty buffer
    buffer[0] = '\0';

    // Loop through each topic and append it to the buffer
    for (int i = 0; i < subscriber->topic_count; i++)
    {
        // If buffer isn't empty, add a comma before appending the next topic
        if (i > 0)
        {
            strncat(buffer, ",", sizeof(buffer) - strlen(buffer) - 1); // Add a comma between topics
        }

        // Append the topic name to the buffer
        strncat(buffer, subscriber->topics[i], sizeof(buffer) - strlen(buffer) - 1);
    }

    printf("Subscriber subscribed to topics: %s\n", buffer);
    if (send(subscriber->sockfd, buffer, strlen(buffer), 0) < 0)
    {
        perror("Failed to send subscription info");
        return NULL;
    }

    // Step 2: Listen for data related to subscribed topics
    while ((bytes_received = recv(subscriber->sockfd, buffer, sizeof(buffer) - 1, 0)) > 0)
    {
        buffer[bytes_received] = '\0'; // Null-terminate the received data
        printf("Subscriber received data: %s\n", buffer);

        // Process the received data based on the topic
        // Here we assume the data is in JSON format and can be parsed accordingly
        cJSON *root = cJSON_Parse(buffer);
        if (root != NULL)
        {
            cJSON *source = cJSON_GetObjectItem(root, "source");
            if (source != NULL)
            {
                cJSON *name = cJSON_GetObjectItem(source, "name");
                if (name != NULL)
                {
                    printf("Data is related to topic: %s\n", name->valuestring);
                }
            }
            cJSON_Delete(root);
        }
    }
    return NULL;
}

// Main function to run the subscriber
int main()
{
    pthread_t subscriber_threads[NO_OF_SUBSCRIBERS];
    Subscriber *subscribers[NO_OF_SUBSCRIBERS];

    // Subscriber 1 subscribes to Reuters and CNN
    subscribers[0] = malloc(sizeof(Subscriber));
    subscribers[0]->sockfd = socket(AF_INET, SOCK_STREAM, 0);
    subscribers[0]->topic_count = 2;
    subscribers[0]->topics[0] = "Reuters";
    subscribers[0]->topics[1] = "CNN";

    // Subscriber 2 subscribes to BBC, Reuters, and CNN
    subscribers[1] = malloc(sizeof(Subscriber));
    subscribers[1]->sockfd = socket(AF_INET, SOCK_STREAM, 0);
    subscribers[1]->topic_count = 3;
    subscribers[1]->topics[0] = "BBC";
    subscribers[1]->topics[1] = "Reuters";
    subscribers[1]->topics[2] = "CNN";

    // Subscriber 3 subscribes only to Reuters
    subscribers[2] = malloc(sizeof(Subscriber));
    subscribers[2]->sockfd = socket(AF_INET, SOCK_STREAM, 0);
    subscribers[2]->topic_count = 1;
    subscribers[2]->topics[0] = "Reuters";

    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(PORT_SUBSCRIBER); // Broker's listening port
    server_address.sin_addr.s_addr = INADDR_ANY;

    // Connect all subscribers to the broker
    for (int i = 0; i < NO_OF_SUBSCRIBERS; i++)
    {
        if (connect(subscribers[i]->sockfd, (struct sockaddr *)&server_address, sizeof(server_address)) < 0)
        {
            perror("Connection to broker failed");
            return -1;
        }
        printf("Subscriber %d connected to broker!\n", i + 1);
    }

    // Create threads for each subscriber
    for (int i = 0; i < NO_OF_SUBSCRIBERS; i++)
    {
        pthread_create(&subscriber_threads[i], NULL, handle_subscriber, (void *)subscribers[i]);
    }

    // Wait for all subscriber threads to finish
    for (int i = 0; i < NO_OF_SUBSCRIBERS; i++)
    {
        pthread_join(subscriber_threads[i], NULL);
    }

    // Cleanup
    for (int i = 0; i < NO_OF_SUBSCRIBERS; i++)
    {
        close(subscribers[i]->sockfd);
        free(subscribers[i]);
    }

    return 0;
}
