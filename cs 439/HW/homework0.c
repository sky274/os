#include<stdio.h>
#include<stdlib.h>

void Print();
struct Node* getNewNode(int x);
void add(int x);
int removeData();

struct Node {
  int data;
  struct Node* next;
  struct Node* prev;
};

struct Node* head;// global variable for head of a node.

// function to create a new node for adding.
struct Node* getNewNode(int x){
  struct Node* newNode = (struct Node*)malloc(sizeof(struct Node));
  newNode-> data = x;
  newNode-> next = NULL;
  newNode-> prev = NULL;
  return newNode;
}

// function to add a new node in ascending order.
void add(int x){
  struct Node* temp = head;
  struct Node* newNode= getNewNode(x);
  if(head == NULL){
    head= newNode;
    return;
  }
  while(temp->next != NULL)
    temp= temp-> next;
  if(temp->data < newNode ->data){
    temp->next = newNode;
    newNode->prev = temp;
  }
  else{
    temp-> prev-> next = newNode;
    newNode-> prev = temp-> prev;
    temp-> prev = newNode;
    newNode-> next = temp;
  }
}

//function to remove the beginning of the list.
int removeData(){
  head->next->prev = NULL;
  head = head-> next;
  return 0;
}

//function to print all the data stored in the node.
void Print() {
  struct Node* temp = head;
  printf("List: ");
  while(temp != NULL) {
    printf("%d ",temp->data);
    temp = temp->next;
  }
  printf("\n");
}

int main() {

head = NULL;

add(1);
add(2);
add(3);
add(4);
add(5);
add(7);
add(9);
Print();
removeData();
Print();

}