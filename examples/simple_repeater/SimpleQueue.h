#pragma once

#define QUEUE_SIZE 5

template <typename T, int SIZE = QUEUE_SIZE>
class SimpleQueue {
public:
  bool enqueue(const T& item) {
    if (count >= SIZE) {
      return false;
    }

    queue[tail] = item;
    tail = (tail + 1) % SIZE;
    count++;
    return true;
  }

  bool dequeue(T& item) {
    if (isEmpty()) {
      return false;
    }

    item = queue[head];
    head = (head + 1) % SIZE;
    count--;
    return true;
  }

  void clear() {
    head = 0;
    tail = 0;
    count = 0;
  }

  T* get_next() {
    if (isEmpty()) {
      return nullptr;
    }

    return &queue[head];
  }

  bool isEmpty() const {
    return count == 0;
  }

  bool isFull() const {
    return count == SIZE;
  }
private:
  T queue[SIZE];
  int head = 0;
  int tail = 0;
  int count = 0;
};