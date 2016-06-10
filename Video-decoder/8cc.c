/***************************************************
 Find connected components
 ---------------------------------------------------
 Input:
        -> Binary image to be labeled
        -> Image height
        -> Image width
 ---------------------------------------------------
 Output:
        -> The input binary image gets labeled
           (Output labels are in sequence:
           {1, 2, 3, ...} and not {1, 3, 4, ...})
        -> Returns number of connected components
           found
 ---------------------------------------------------
 Created by:  Abhishek on 06/17/2015.
 Modified by: Abhishek on 08/19/2015.
 Copyright (c) 2015 e-Lab. All rights reserved.
 ***************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Linked list structure for storing relevance values of different labels
struct equivalenceNode{
    int label;
    struct equivalenceNode *next;
};

// Primary equivalene tree
struct node{
    int label;
    struct equivalenceNode *withinEquivalence;
    struct node *next;
};

// Function definition

int connectedComponent(int *, int *, int, int, int);

struct node *createRoot(struct node *, int);                                        // Create the first node
struct node *primaryLabels(struct node *, int);                                     // Create the list of all labels which are being used

struct node *equivalentLabels(struct node *, int, int);                             // Create the list of equivalent labels for the given label
struct equivalenceNode *insertEquivalentLabels(struct equivalenceNode *, int);      // Add new labels to the CURRENT equivalent label list

struct node *updateLabel(struct node *, int, int);                                  // Add new labels to the TARGET equivalent label list
struct node *homogenizeEquivalentLabels(struct node *);                             // Ensures that all the equivalent labels have relevant labels for update
int findInList(struct equivalenceNode *, int);                                      // Search label in existing equivalent list
int getUpdatedLabel(struct node*, int);                                             // Acquire the relevant equivalent label for the given label

void display(struct node *);                                                        // Display the whole table of equivalent label
void displayEquivalentList(struct equivalenceNode *);                               // Display equivalent label of the current label


int connectedComponent(int *image, int* coordinates, int coordinatesSize, int height, int width) {
    int *nw, *n, *ne, *w, *current;
    int currentLabel = 1;
    int labeledImage[height][width];

    int i, j, r, c, labelCount, minLabel, previousLabel;
    memset(labeledImage, 0, sizeof(labeledImage[0][0]) * height * width);

    struct node *root = NULL;

    //First pass
    for (r = 1; r < height-1; r++) {
        for (c = 1; c < width-1; c++) {
            nw = ((image+(r-1)*width) + (c-1));
            n = ((image+(r-1)*width) + (c));
            ne = ((image+(r-1)*width) + (c+1));
            w = ((image+(r)*width) + (c-1));
            current = ((image+r*width) + c);

            minLabel = 100;
            if (*current == 1) {
                if (*nw == 0 && *n == 0 && *ne == 0 && *w == 0) {
                    labeledImage[r][c] = currentLabel;
                    if (currentLabel == 1) {
                        root = createRoot(root, currentLabel);
                    }
                    else {
                        root = primaryLabels(root, currentLabel);
                    }
                    currentLabel++;
                }
                else{
                    for (i = -1; i < 1; i++){
                        for (j = -1; j < 2; j++){
                            if (i >= 0 && j >= 0){
                                break;
                            }
                            if (labeledImage[r+i][c+j] != 0) {
                                if(labeledImage[r+i][c+j] < minLabel){
                                    if (minLabel == 100) {
                                        root = equivalentLabels(root, labeledImage[r+i][c+j], labeledImage[r+i][c+j]);
                                    }
                                    else {
                                        root = equivalentLabels(root, labeledImage[r+i][c+j], minLabel);
                                    }
                                    minLabel = labeledImage[r+i][c+j];
                                    labeledImage[r][c] = minLabel;
                                }
                                else {
                                    root = equivalentLabels(root, minLabel, labeledImage[r+i][c+j]);
                                }

                                if (minLabel == 100) {
                                    root = equivalentLabels(root, labeledImage[r+i][c+j], labeledImage[r+i][c+j]);
                                }
                                else {
                                    root = equivalentLabels(root, labeledImage[r+i][c+j], minLabel);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    root = homogenizeEquivalentLabels(root);

    if (root != NULL) {
        int labelArr[currentLabel];
        int labelIndex[currentLabel];
        int finalLabels[currentLabel];

        for (i = 1; i < currentLabel; i++) {
            labelArr[i] = getUpdatedLabel(root, i);
        }

        for (i = 1; i < currentLabel; i++) {
            labelIndex[i] = i;
        }

        // Sorting
        for (i = 1; i < currentLabel-1; i++) {
            for (j = i+1; j < currentLabel; j++) {
                if (labelArr[j] < labelArr[i]) {
                    int temp = labelArr[i];
                    labelArr[i] = labelArr[j];
                    labelArr[j] = temp;
                    temp = labelIndex[j];
                    labelIndex[j] = labelIndex[i];
                    labelIndex[i] = temp;
                }
            }
        }

        previousLabel = labelArr[1];
        int labelToUse = 1;
        for (i = 1; i < currentLabel; i++) {
            if (labelArr[i] != previousLabel) {
                previousLabel = labelArr[i];
                labelToUse++;
            }
            labelArr[i] = labelToUse;
        }

        for (i = 1; i < currentLabel; i++) {
            finalLabels[labelIndex[i]] = labelArr[i];
        }

        // Second Pass
        for (i = 1; i < height-1; i++) {
            for (j = 1; j < width-1; j++) {
                current = ((image+i*width) + j);

                if (*current == 1) {
                    labeledImage[i][j] = finalLabels[labeledImage[i][j]];
                    *((image+i*width) + j) = labeledImage[i][j];
                }
            }
        }
        for (labelCount = 1; labelCount-1 < coordinatesSize && labelCount <= labelArr[currentLabel-1]; labelCount++) {
            int minR = 10000, minC = 100000, maxR = 0, maxC = 0;
            for (i = 1; i < height-1; i++) {
                for (j = 1; j < width-1; j++) {
                    if (labeledImage[i][j] == labelCount) {
                        if (i < minR) {
                            minR = i;
                        }
                        if (i > maxR) {
                            maxR = i;
                        }
                        if (j < minC) {
                            minC = j;
                        }
                        if (j > maxC) {
                            maxC = j;
                        }
                    }
                }
            }
            *((coordinates+(labelCount-1)*4) + 0) = minC;
            *((coordinates+(labelCount-1)*4) + 1) = minR;
            *((coordinates+(labelCount-1)*4) + 2) = maxC;
            *((coordinates+(labelCount-1)*4) + 3) = maxR;
        }

        return labelCount-1;
    }

    return (0);
}

int getUpdatedLabel(struct node *root, int target) {
    int label;

    struct node *current = root;
    while (current->label < target) {
        current = current->next;
    }

    label = current->withinEquivalence->label;

    return label;
}

struct node *createRoot(struct node *root, int value){
    root = malloc(sizeof(struct node));
    root->label = value;
    root->next = NULL;
    root->withinEquivalence = malloc(sizeof(struct equivalenceNode));
    root->withinEquivalence->label = value;
    root->withinEquivalence->next = NULL;

    return root;
}


struct node *primaryLabels(struct node *root, int value){
    struct node *current = root;
    struct node *newNode = NULL;
    newNode = createRoot(newNode, value);

    if (current->label > value) {
        newNode->next = current;
        root = newNode;
    }
    else{
        while (current->next != NULL && current->next->label < value) {
            current = current->next;
        }

        newNode->next = current->next;
        current->next = newNode;
    }

    return root;
}

struct equivalenceNode *insertEquivalentLabels(struct equivalenceNode *root, int value){
    if (findInList(root, value) == 0) {
        struct equivalenceNode *current = root;
        struct equivalenceNode *newNode = malloc(sizeof(struct equivalenceNode));
        newNode->label = value;
        if (current->label > value) {
            newNode->next = current;
            root = newNode;
        }
        else{
            while (current->next != NULL && current->next->label < value) {
                current = current->next;
            }

            newNode->next = current->next;
            current->next = newNode;
        }
    }
    return root;
}

// Value1 < Value2
struct node *equivalentLabels(struct node *root, int value1, int value2){
    struct node *current = root;
    while (current != NULL) {
        if (current->label == value1) {
            current->withinEquivalence = insertEquivalentLabels(current->withinEquivalence, value2);
        }
        if (current->label == value2) {
            current->withinEquivalence = insertEquivalentLabels(current->withinEquivalence, value1);
            break;
        }
        current = current->next;
    }
    return root;
}

struct node *updateLabel(struct node *root, int target, int addLabel) {
    struct node *current = root;

    while (current->label < target) {
        current = current->next;
    }
    current->withinEquivalence = insertEquivalentLabels(current->withinEquivalence, addLabel);
    return root;
}


struct node *homogenizeEquivalentLabels(struct node *root) {
    struct node *current = root;
    struct equivalenceNode *withinCurrent;
    int addLabel;

    while (current != NULL) {
        withinCurrent = current->withinEquivalence;
        addLabel = withinCurrent->label;
        while (withinCurrent != NULL) {
            root = updateLabel(root, withinCurrent->label, addLabel);
            withinCurrent = withinCurrent->next;
        }
        current = current->next;
    }
    return root;
}

int findInList(struct equivalenceNode *root, int value) {
    struct equivalenceNode *current = root;
    while (current != NULL) {
        if (current->label == value) {
            return 1;
        }
        current = current->next;
    }
    return 0;
}

void display(struct node *root){
    struct node *current = root;
    while (current != NULL) {
        printf("%d:\n   ", current->label);
        displayEquivalentList(current->withinEquivalence);
        current = current->next;
        printf("\n");
    }
}

void displayEquivalentList(struct equivalenceNode *root){
    struct equivalenceNode *current = root;
    while (current != NULL) {
        printf("%d -> ", current->label);
        current = current->next;
    }
}
