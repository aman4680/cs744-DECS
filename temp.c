#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <cjson/cJSON.h>

#define MAX_TOPICS 10
#define MAX_SUBSCRIBERS 100
#define MAX_DATA 512
#define MAX_BUFFER_SIZE 8192
#define PORT_SUBSCRIBER 8080
#define PORT_PUBLISHER 8081

// Data structure for a subscriber
typedef struct
{
    int sockfd;
    char *topics[MAX_TOPICS];
    int topic_count;
    int data_read[MAX_TOPICS]; // Tracks which data has been read for each topic
} Subscriber;

// Data structure for a topic
typedef struct
{
    char *name;
    Subscriber *subscribers[MAX_SUBSCRIBERS];
    int subscriber_count;
    cJSON *data[MAX_DATA]; // Data (news articles) for this topic
    int data_count;
    int data_read_count[MAX_DATA]; // Tracks how many subscribers have read a piece of data
    pthread_mutex_t mutex;         // Mutex for locking topic operations
} Topic;

Topic topics[MAX_TOPICS]; // Broker stores predefined topics
int topic_count = 0;
pthread_mutex_t topic_mutex[MAX_TOPICS]; // Mutex array for each topic

void printTopicsWithDetails(Topic *topics)
{
    for (int i = 0; i < topic_count; i++)
    {
        // Print the topic name
        printf("Topic: %s\n", topics[i].name);

        // Print the subscribers for the topic
        printf("Subscribers:\n");
        if (topics[i].subscriber_count > 0)
        {
            for (int j = 0; j < topics[i].subscriber_count; j++)
            {
                printf("\tSubscriber #%d (sockfd: %d)\n", j + 1, topics[i].subscribers[j]->sockfd);
            }
        }
        else
        {
            printf("\tNo subscribers for this topic.\n");
        }

        // Print the data associated with the topic
        printf("Data for Topic '%s':\n", topics[i].name);
        if (topics[i].data_count > 0)
        {
            for (int k = 0; k < topics[i].data_count; k++)
            {
                // Assuming you want to print the JSON data, or some representative part of it
                // You can format the JSON object into a string to print it
                char *json_str = cJSON_Print(topics[i].data[k]); // Convert the JSON object to a string
                if (json_str != NULL)
                {
                    printf("\tData #%d: %s\n", k + 1, json_str);
                    free(json_str); // Free the string after printing
                }
            }
        }
        else
        {
            printf("\tNo data available for this topic.\n");
        }

        // // Print the read counts for the data
        // printf("Data read counts:\n");
        // for (int m = 0; m < topics[i].data_count; m++)
        // {
        //     printf("\tData #%d read by %d subscribers.\n", m + 1, topics[i].data_read_count[m]);
        // }

        printf("\n"); // Separate topics with a newline for readability
    }
}

// Function to initialize the mutexes for each topic
void init_topic_mutexes()
{
    for (int i = 0; i < MAX_TOPICS; i++)
    {
        pthread_mutex_init(&topic_mutex[i], NULL);
    }
}

// Function to destroy the mutexes for each topic
void destroy_topic_mutexes()
{
    for (int i = 0; i < MAX_TOPICS; i++)
    {
        pthread_mutex_destroy(&topic_mutex[i]);
    }
}

// Function to add a new subscriber to a topic
void add_subscriber_to_topic(Topic *topic, Subscriber *subscriber)
{
    pthread_mutex_lock(&topic->mutex);
    if (topic->subscriber_count < MAX_SUBSCRIBERS)
    {
        topic->subscribers[topic->subscriber_count++] = subscriber;
    }
    pthread_mutex_unlock(&topic->mutex);
}

// Function to add new data to a topic
void add_data_to_topic(Topic *topic, cJSON *data)
{
    pthread_mutex_lock(&topic->mutex);
    if (topic->data_count < MAX_DATA)
    {
        topic->data[topic->data_count] = data;
        topic->data_read_count[topic->data_count] = 0; // Initially, no subscribers have read the data
        topic->data_count++;
    }
    pthread_mutex_unlock(&topic->mutex);
}

// Function to process received data (extract topic and forward to relevant subscribers)
void process_data_from_publisher(const char *json_data)
{
    cJSON *root = cJSON_Parse(json_data);
    if (root == NULL)
    {
        fprintf(stderr, "Error parsing JSON\n");
        return;
    }

    cJSON *source = cJSON_GetObjectItem(root, "source");
    if (source == NULL)
    {
        fprintf(stderr, "No source found in JSON\n");
        cJSON_Delete(root);
        return;
    }

    cJSON *name = cJSON_GetObjectItem(source, "name");
    if (name == NULL)
    {
        fprintf(stderr, "No name found in source\n");
        cJSON_Delete(root);
        return;
    }

    // Find or create the topic
    Topic *topic = NULL;
    pthread_mutex_t *mutex = NULL;
    for (int i = 0; i < topic_count; i++)
    {
        if (strcmp(topics[i].name, name->valuestring) == 0)
        {
            topic = &topics[i];
            mutex = &topic_mutex[i];
            break;
        }
    }

    // If topic doesn't exist, create it
    if (topic == NULL && topic_count < MAX_TOPICS)
    {
        topic = &topics[topic_count++];
        printf("%s\n", name->valuestring);
        topic->name = strdup(name->valuestring);
        topic->subscriber_count = 0;
        topic->data_count = 0;
        pthread_mutex_init(&topic->mutex, NULL); // Initialize mutex for the new topic
        mutex = &topic_mutex[topic_count - 1];
    }

    // Add the data to the topic
    add_data_to_topic(topic, root); // Store the data under the correct topic

    // Send the data to all subscribers of this topic
    // pthread_mutex_lock(mutex);
    // for (int i = 0; i < topic->subscriber_count; i++)
    // {
    //     send(topic->subscribers[i]->sockfd, json_data, strlen(json_data), 0);
    // }
    // pthread_mutex_unlock(mutex);
}

// Function to handle incoming connections from subscribers
void *handle_subscriber(void *arg)
{
    Subscriber *subscriber = (Subscriber *)arg;
    char buffer[MAX_BUFFER_SIZE];
    int bytes_received;

    // Receive and handle subscription information (topics the subscriber is interested in)
    while ((bytes_received = recv(subscriber->sockfd, buffer, sizeof(buffer) - 1, 0)) > 0)
    {
        buffer[bytes_received] = '\0'; // Null-terminate the received string
        printf("Subscriber subscribed to topics: %s\n", buffer);

        // Parse the topics and add the subscriber to those topics
        char *token = strtok(buffer, ",");
        while (token != NULL)
        {
            for (int i = 0; i < topic_count; i++)
            {
                if (strcmp(topics[i].name, token) == 0)
                {
                    printf("Topic name: %s, token: %s\n", topics[i].name, token);
                    add_subscriber_to_topic(&topics[i], subscriber);
                    subscriber->topics[subscriber->topic_count++] = topics[i].name;
                }
            }
            token = strtok(NULL, ",");
        }

        // for (int i = 0; i < topic_count; i++)
        // {
        //     printf("Subscribers:\n");
        //     if (topics[i].subscriber_count > 0)
        //     {
        //         for (int j = 0; j < topics[i].subscriber_count; j++)
        //         {
        //             printf("\tSubscriber #%d (sockfd: %d)\n", j + 1, topics[i].subscribers[j]->sockfd);
        //         }
        //     }
        // }

        printf("Granted subscription request\n");

        break;
    }

    // Step 2: Send data for the subscribed topics
    // int no_of_topics_of_this_subscriber = subscriber->topic_count;
    // while (no_of_topics_of_this_subscriber--)
    // {
    // Loop through all topics and send data to subscribers
    for (int i = 0; i < topic_count; i++)
    {
        pthread_mutex_lock(&topics[i].mutex);
        printf("Topic name: %s, No of subscribers for this topic: %d\n", topics[i].name, topics[i].subscriber_count);
        for (int j = 0; j < topics[i].subscriber_count; j++)
        {
            // Send the data related to this topic to all subscribers
            if (send(topics[i].subscribers[j]->sockfd, cJSON_Print(topics[i].data[j]), strlen(cJSON_Print(topics[i].data[j])), 0) < 0)
            {
                perror("Failed to send data to subscriber");
            }
            printf("Sent data to subscriber #%d for topic: %s\n", j + 1, topics[i].name);
        }
        pthread_mutex_unlock(&topics[i].mutex);
    }

    // // Sleep or wait for new data to arrive for topics
    // sleep(1); // Adjust sleep as needed
    // }

    // Handle receiving data from topics
    // while ((bytes_received = recv(subscriber->sockfd, buffer, sizeof(buffer) - 1, 0)) > 0)
    // {
    //     buffer[bytes_received] = '\0'; // Null-terminate the received string
    //     printf("Subscriber reading data: %s\n", buffer);

    //     // Parse the received data (assumes it has a source field)
    //     cJSON *root = cJSON_Parse(buffer);
    //     if (root != NULL)
    //     {
    //         cJSON *source = cJSON_GetObjectItem(root, "source");
    //         if (source != NULL)
    //         {
    //             cJSON *name = cJSON_GetObjectItem(source, "name");
    //             if (name != NULL)
    //             {
    //                 // Find the topic and mark the data as read for this subscriber
    //                 for (int i = 0; i < topic_count; i++)
    //                 {
    //                     if (strcmp(topics[i].name, name->valuestring) == 0)
    //                     {
    //                         pthread_mutex_lock(&topics[i].mutex);
    //                         for (int j = 0; j < topics[i].data_count; j++)
    //                         {
    //                             if (strcmp(cJSON_GetObjectItem(cJSON_GetArrayItem(topics[i].data[j], j), "url")->valuestring,
    //                                        cJSON_GetObjectItem(root, "url")->valuestring) == 0)
    //                             {
    //                                 topics[i].data_read_count[j]++;

    //                                 // Check if all subscribers have read this data
    //                                 if (topics[i].data_read_count[j] == topics[i].subscriber_count)
    //                                 {
    //                                     printf("All subscribers have read the data, removing it\n");
    //                                     // Remove the data from the topic
    //                                     cJSON_Delete(topics[i].data[j]);
    //                                     for (int k = j; k < topics[i].data_count - 1; k++)
    //                                     {
    //                                         topics[i].data[k] = topics[i].data[k + 1];
    //                                         topics[i].data_read_count[k] = topics[i].data_read_count[k + 1];
    //                                     }
    //                                     topics[i].data_count--;
    //                                 }
    //                                 break;
    //                             }
    //                         }
    //                         pthread_mutex_unlock(&topics[i].mutex);
    //                     }
    //                 }
    //             }
    //         }
    //         cJSON_Delete(root);
    //     }
    // }

    // Subscriber disconnected
    printf("Subscriber disconnected\n");
    close(subscriber->sockfd);
    free(subscriber);
    return NULL;
}

// Function to handle incoming connections from the publisher
void *handle_publisher(void *arg)
{
    int publisher_sockfd = *((int *)arg);
    char buffer[MAX_BUFFER_SIZE];
    int bytes_received;

    // Receive and process data from the publisher
    while ((bytes_received = recv(publisher_sockfd, buffer, sizeof(buffer) - 1, 0)) > 0)
    {
        buffer[bytes_received] = '\0'; // Null-terminate the received string
        printf("Received data from publisher: %s\n", buffer);
        process_data_from_publisher(buffer); // Process the data and forward to relevant topics/subscribers
    }

    // Publisher disconnected
    printf("Publisher disconnected\n");
    close(publisher_sockfd);
    return NULL;
}

// Main function for broker server
int main()
{
    int server_fd_subscriber, server_fd_publisher, new_sock_subscriber, new_sock_publisher;
    struct sockaddr_in address_subscriber, address_publisher;
    int addrlen_subscriber = sizeof(address_subscriber), addrlen_publisher = sizeof(address_publisher);

    // Initialize mutexes
    init_topic_mutexes();

    // Create socket for subscribers
    if ((server_fd_subscriber = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("Subscriber socket failed");
        exit(1);
    }
    address_subscriber.sin_family = AF_INET;
    address_subscriber.sin_addr.s_addr = INADDR_ANY;
    address_subscriber.sin_port = htons(PORT_SUBSCRIBER);

    if (bind(server_fd_subscriber, (struct sockaddr *)&address_subscriber, sizeof(address_subscriber)) < 0)
    {
        perror("Bind failed");
        exit(1);
    }
    if (listen(server_fd_subscriber, 3) < 0)
    {
        perror("Listen failed");
        exit(1);
    }

    // Create socket for publishers
    if ((server_fd_publisher = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("Publisher socket failed");
        exit(1);
    }
    address_publisher.sin_family = AF_INET;
    address_publisher.sin_addr.s_addr = INADDR_ANY;
    address_publisher.sin_port = htons(PORT_PUBLISHER);

    if (bind(server_fd_publisher, (struct sockaddr *)&address_publisher, sizeof(address_publisher)) < 0)
    {
        perror("Bind failed");
        exit(1);
    }
    if (listen(server_fd_publisher, 3) < 0)
    {
        perror("Listen failed");
        exit(1);
    }

    printf("Broker is running...\n");

    // Accept publisher connections
    while (1)
    {
        // Accept new publisher connection
        new_sock_publisher = accept(server_fd_publisher, (struct sockaddr *)&address_publisher, (socklen_t *)&addrlen_publisher);
        if (new_sock_publisher < 0)
        {
            perror("Accept failed");
            exit(1);
        }

        // Create a new publisher thread
        pthread_t publisher_thread;
        pthread_create(&publisher_thread, NULL, handle_publisher, (void *)&new_sock_publisher);

        pthread_join(publisher_thread, NULL);
        printf("Topics details\n");
        printTopicsWithDetails(topics);

        // Accept new subscriber connection
        new_sock_subscriber = accept(server_fd_subscriber, (struct sockaddr *)&address_subscriber, (socklen_t *)&addrlen_subscriber);
        if (new_sock_subscriber < 0)
        {
            perror("Accept failed");
            exit(1);
        }

        // Create a new subscriber thread
        Subscriber *new_subscriber = malloc(sizeof(Subscriber));
        new_subscriber->sockfd = new_sock_subscriber;
        new_subscriber->topic_count = 0;
        pthread_t subscriber_thread;
        pthread_create(&subscriber_thread, NULL, handle_subscriber, (void *)new_subscriber);
    }

    // Cleanup and close server
    destroy_topic_mutexes();
    return 0;
}
