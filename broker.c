#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <cjson/cJSON.h>
#include <sys/epoll.h>

#define MAX_TOPICS 3
#define MAX_SUBSCRIBERS 100
#define MAX_DATA 512
#define MAX_BUFFER_SIZE 8192
#define PORT_SUBSCRIBER 8080
#define PORT_PUBLISHER 8081
#define MAX_EVENTS 10

// Data structure for a subscriber
typedef struct
{
    int sockfd;               // Acts as identifier for the subscriber
    char *topics[MAX_TOPICS]; // Topic names to which the subscriber has subscribed to
    int topic_count;          // No of topics to which this subscriber has subscribed to
    pthread_cond_t cond;      // Condition variable for waiting for data in subscribed topics
} Subscriber;

// Data structure for a topic
typedef struct
{
    char *name;                               // Name of topic
    Subscriber *subscribers[MAX_SUBSCRIBERS]; // List of subscribers
    int subscriber_count;                     // Count of subscribers subscribed to this topic
    cJSON *data[MAX_DATA];                    // Data (news articles) for this topic
    int data_count;                           // No of data items in a specific topic
    pthread_mutex_t mutex;                    // Mutex for locking topic operations
    pthread_cond_t cond;                      // Condition variable for waiting for new data
} Topic;

Topic topics[MAX_TOPICS]; // Broker stores predefined topics
int topic_count = 0;
int publisher_done = 0;
pthread_mutex_t topic_mutex[MAX_TOPICS]; // Mutex array for each topic

void printTopicsWithDetails(Topic *topics)
{
    for (int i = 0; i < topic_count; i++)
    {
        // Print the topic name
        printf("Topic: %s\n", topics[i].name);

        // Print the data associated with the topic
        printf("Data for Topic '%s':\n", topics[i].name);
        for (int k = 0; k < topics[i].data_count; k++)
        {
            char *json_str = cJSON_Print(topics[i].data[k]); // Convert the JSON object to a string
            if (json_str != NULL)
            {
                printf("\tData #%d: %s\n", k + 1, json_str);
                free(json_str); // Free the string after printing
            }
        }

        printf("\n"); // Separate topics with a newline for readability
    }
}

// Function to initialize the mutexes and condition variables for each topic
void init_topic_mutexes_and_cond()
{
    for (int i = 0; i < MAX_TOPICS; i++)
    {
        pthread_mutex_init(&topic_mutex[i], NULL);
        pthread_cond_init(&topics[i].cond, NULL);
    }
}

// Function to destroy the mutexes and condition variables for each topic
void destroy_topic_mutexes_and_cond()
{
    for (int i = 0; i < MAX_TOPICS; i++)
    {
        pthread_mutex_destroy(&topic_mutex[i]);
        pthread_cond_destroy(&topics[i].cond);
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
        topic->data_count++;
    }
    // pthread_cond_broadcast(&topic->cond); // Wake up waiting subscribers
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

    // Find the topic based on the name provided by the publisher
    Topic *topic = NULL;
    for (int i = 0; i < topic_count; i++)
    {
        if (strcmp(topics[i].name, name->valuestring) == 0)
        {
            topic = &topics[i];
            break;
        }
    }

    // If the topic doesn't exist, print an error and do nothing
    if (topic == NULL)
    {
        fprintf(stderr, "Topic '%s' does not exist. No data will be added.\n", name->valuestring);
        cJSON_Delete(root); // Clean up the JSON object
        return;
    }

    // Add the data to the topic
    add_data_to_topic(topic, root); // Store the data under the correct topic
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

    // Notify all subscribers about the new data
    for (int i = 0; i < topic_count; i++)
    {
        // Subscriber *subscriber = topics->subscribers[i];
        // Wake up the subscriber's waiting thread
        Topic *topic = &topics[i];
        pthread_cond_signal(&topic->cond);
    }

    publisher_done = 1;
    close(publisher_sockfd);
    return NULL;
}

// Function to handle the subscription request from the subscriber
void request_subscription(Subscriber *subscriber, char *buffer)
{
    // Parse the topics and add the subscriber to those topics
    char *token = strtok(buffer, ",");
    while (token != NULL)
    {
        for (int i = 0; i < topic_count; i++)
        {
            if (strcmp(topics[i].name, token) == 0)
            {
                // Add the subscriber to the topic
                add_subscriber_to_topic(&topics[i], subscriber);
                // Store the topic in the subscriber's list of topics
                subscriber->topics[subscriber->topic_count++] = topics[i].name;
                printf("Subscriber subscribed to topic: %s\n", token);
            }
        }
        token = strtok(NULL, ",");
    }
}

// Function to wait for data on subscribed topics and send it to the subscriber
void wait_for_data_and_send(Subscriber *subscriber)
{
    // Wait for and send the data for current subscriptions
    for (int i = 0; i < subscriber->topic_count; i++)
    {
        Topic *topic = NULL;
        for (int j = 0; j < topic_count; j++)
        {
            if (strcmp(topics[j].name, subscriber->topics[i]) == 0)
            {
                topic = &topics[j];
                break;
            }
        }

        if (topic != NULL)
        {
            pthread_mutex_lock(&topic->mutex);

            while (topic->data_count == 0)
            {
                // Wait for new data to arrive for this topic
                pthread_cond_wait(&topic->cond, &topic->mutex);
            }

            // Send all data related to this topic to the subscriber
            for (int j = 0; j < topic->data_count; j++)
            {
                if (send(subscriber->sockfd, cJSON_Print(topic->data[j]), strlen(cJSON_Print(topic->data[j])), 0) < 0)
                {
                    perror("Failed to send data to subscriber");
                }
                printf("Sent data #%d for topic: %s\n", j + 1, topic->name);
            }

            pthread_mutex_unlock(&topic->mutex);
        }
    }
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
        printf("Subscriber requested to subscribe to topics: %s\n", buffer);

        // Request subscription based on the received buffer
        request_subscription(subscriber, buffer);

        break; // Once subscription is handled, we break the loop
    }

    // After subscription, wait for and send the data for subscribed topics
    wait_for_data_and_send(subscriber);

    // Subscriber disconnected
    printf("Subscriber disconnected\n");
    close(subscriber->sockfd);
    free(subscriber);
    return NULL;
}

// Main function for broker server
int main()
{
    int server_fd_subscriber, server_fd_publisher, new_sock_subscriber, new_sock_publisher;
    struct sockaddr_in address_subscriber, address_publisher;
    int addrlen_subscriber = sizeof(address_subscriber), addrlen_publisher = sizeof(address_publisher);

    // Initialize mutexes and condition variables
    init_topic_mutexes_and_cond();

    // Predefine 3 topics
    topics[0].name = "Reuters";
    topics[1].name = "BBC";
    topics[2].name = "CNN";
    topic_count = 3;

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

    // Create epoll instance
    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1)
    {
        perror("Epoll create failed");
        exit(1);
    }

    // Add subscriber and publisher sockets to epoll instance
    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = server_fd_subscriber;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd_subscriber, &event) == -1)
    {
        perror("Epoll ctl failed (subscriber)");
        exit(1);
    }

    event.data.fd = server_fd_publisher;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd_publisher, &event) == -1)
    {
        perror("Epoll ctl failed (publisher)");
        exit(1);
    }

    struct epoll_event events[MAX_EVENTS];
    printf("Broker is running...\n");

    // Start processing events
    while (1)
    {
        int n = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (n == -1)
        {
            perror("Epoll wait failed");
            exit(1);
        }

        // Loop through all the events
        for (int i = 0; i < n; i++)
        {
            if (events[i].data.fd == server_fd_publisher)
            {
                // Handle publisher connection
                new_sock_publisher = accept(server_fd_publisher, (struct sockaddr *)&address_publisher, (socklen_t *)&addrlen_publisher);
                if (new_sock_publisher < 0)
                {
                    perror("Publisher accept failed");
                    continue;
                }

                // Handle publisher in a separate thread
                pthread_t publisher_thread;
                pthread_create(&publisher_thread, NULL, handle_publisher, (void *)&new_sock_publisher);
            }
            else if (events[i].data.fd == server_fd_subscriber)
            {
                // Handle subscriber connection
                printf("Handling a new subscriber\n");
                new_sock_subscriber = accept(server_fd_subscriber, (struct sockaddr *)&address_subscriber, (socklen_t *)&addrlen_subscriber);
                if (new_sock_subscriber < 0)
                {
                    perror("Subscriber accept failed");
                    continue;
                }

                // Handle subscriber in a separate thread
                Subscriber *new_subscriber = malloc(sizeof(Subscriber));
                new_subscriber->sockfd = new_sock_subscriber;
                new_subscriber->topic_count = 0;
                pthread_t subscriber_thread;
                pthread_create(&subscriber_thread, NULL, handle_subscriber, (void *)new_subscriber);
                pthread_detach(subscriber_thread);
            }
        }
    }

    // Cleanup
    destroy_topic_mutexes_and_cond();
    close(epoll_fd);
    return 0;
}
