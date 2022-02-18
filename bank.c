#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include "options.h"

#define MAX_AMOUNT 20

struct bank {
    int num_accounts;        // number of accounts
    int *accounts;           // balance array
    pthread_mutex_t *mutex;
};

struct args {
    int          thread_num;  // application defined thread #
    int          delay;       // delay between operations
    int	         iterations;  // number of operations
    int          net_total;   // total amount deposited by this thread
    struct bank *bank;        // pointer to the bank (shared with other threads)
};

struct thread_info {
    pthread_t    id;    // id returned by pthread_create()
    struct args *args;  // pointer to the arguments
};

void processOperation(void *ptr,int acc1,int acc2,int balance,int amount,int operation){
  struct args *args = ptr;

  if(operation){
  balance = args->bank->accounts[acc1];
  if(args->delay) usleep(args->delay); // Force a context switch

  balance -= amount;
  if(args->delay) usleep(args->delay);

  args->bank->accounts[acc1] = balance;
  if(args->delay) usleep(args->delay);
  }

  //reciving account
  balance = args->bank->accounts[acc2];
  if(args->delay) usleep(args->delay); // Force a context switch

  balance += amount;
  if(args->delay) usleep(args->delay);

  args->bank->accounts[acc2] = balance;
  if(args->delay) usleep(args->delay);

  args->net_total += amount;
}

// Threads run on this function
void *deposit(void *ptr)
{
    struct args *args =  ptr;
    int amount, account, balance=0;

    while(args->iterations--) {
        amount  = rand() % MAX_AMOUNT;
        account = rand() % args->bank->num_accounts;
        printf("Thread %d depositing %d on account %d\n",
            args->thread_num, amount, account);

        pthread_mutex_lock(&args->bank->mutex[account]);

        processOperation(ptr,0,account,balance,amount,0);

        pthread_mutex_unlock(&args->bank->mutex[account]);
    }
    return NULL;
}

void skipInterblock(void *ptr,int acc1,int acc2){
  struct args *args = ptr;
  if(acc1<acc2){
      pthread_mutex_lock(&args->bank->mutex[acc1]);
      pthread_mutex_lock(&args->bank->mutex[acc2]);
  }
  else{
      pthread_mutex_lock(&args->bank->mutex[acc2]);
      pthread_mutex_lock(&args->bank->mutex[acc1]);
  }
}

void *transfer(void *ptr)
{
    struct args *args =  ptr;
    int amount, account1, account2, balance=0;

    while(args->iterations--) {

        do{
            account1 = rand() % args->bank->num_accounts;
        }while(0 == (args->bank->accounts[account1]));

        while(account1 == (account2 = rand() % args->bank->num_accounts));

        skipInterblock(ptr,account1,account2);
        amount = rand() % (args->bank->accounts[account1]+1);

        printf("Account %d transfering %d to account %d\n",
            account1, amount, account2);

        processOperation(ptr,account1,account2,balance,amount,1);

        pthread_mutex_unlock(&args->bank->mutex[account2]);
        pthread_mutex_unlock(&args->bank->mutex[account1]);
    }
    return NULL;
}

// start opt.num_threads threads.
struct thread_info *start_threads(struct options opt, struct bank *bank, void *func)
{
    int i;
    struct thread_info *threads;

    printf("\nCreating %d threads\n", opt.num_threads);
    threads = malloc(sizeof(struct thread_info) * (opt.num_threads+1));

    if (threads == NULL) {
        printf("Not enough memory\n");
        exit(1);
    }

    // Create num_thread threads running swap()
    for (i = 0; i < opt.num_threads; i++) {
        threads[i].args = malloc(sizeof(struct args));

        threads[i].args -> thread_num = i;
        threads[i].args -> net_total  = 0;
        threads[i].args -> bank       = bank;
        threads[i].args -> delay      = opt.delay;
        threads[i].args -> iterations = opt.iterations;

        if (0 != pthread_create(&threads[i].id, NULL, func, threads[i].args)) {
            printf("Could not create thread #%d", i);
            exit(1);
        }
    }

    return threads;
}

// start one thread.
struct thread_info start_thread(struct options opt, struct bank *bank, void *func)
{
    struct thread_info thread;
        thread.args = malloc(sizeof(struct args));
        thread.args -> thread_num = 0;
        thread.args -> net_total  = 0;
        thread.args -> bank       = bank;
        thread.args -> delay      = opt.delay;
        thread.args -> iterations = opt.iterations;

        if (0 != pthread_create(&thread.id, NULL, func, thread.args)) {
            printf("Could not create thread");
            exit(1);
        }

    return thread;
}

void lock_all(pthread_mutex_t * mutex, int num){
    for(int i=0; i < num; i++)
        pthread_mutex_lock(&mutex[i]);
}

void unlock_all(pthread_mutex_t * mutex, int num){
    for(int i=0; i < num; i++)
        pthread_mutex_unlock(&mutex[i]);
}

// Print the final balance
void *print_total_balance(void *ptr) {
    int bank_total=0;
    struct args *args =  ptr;

    while(args->iterations--) {
        lock_all(args->bank->mutex,args->bank->num_accounts);
        for(int i=0; i < args->bank->num_accounts; i++) {
            bank_total += args->bank->accounts[i];
        }
        printf("\x1b[31m""Total balance: %d\n""\x1b[0m", bank_total);
        bank_total =0;
        unlock_all(args->bank->mutex,args->bank->num_accounts);
        usleep(500); // Force a context switch
    }
    return NULL;
}

// Print the final balances of accounts
void print_acc_balances(struct bank *bank, struct thread_info *thrs, int num_threads) {
    int bank_total=0;

    printf("\nAccount balance\n");
    for(int i=0; i < bank->num_accounts; i++) {
        printf("%d: %d\n", i, bank->accounts[i]);
        bank_total += bank->accounts[i];
    }
    printf("Total: %d\n", bank_total);
}

// Print the final balances of accounts and threads
void print_thrs_balances(struct bank *bank, struct thread_info *thrs, int num_threads) {
    int total_deposits=0;
    printf("\nNet deposits by thread\n");

    for(int i=0; i < num_threads; i++) {
        printf("%d: %d\n", i, thrs[i].args->net_total);
        total_deposits += thrs[i].args->net_total;
    }
    printf("Total: %d\n", total_deposits);
}

// wait for all threads to finish, print totals, and free memory
void wait(struct options opt, struct bank *bank, struct thread_info *threads) {
    // Wait for the threads to finish
    for (int i = 0; i < opt.num_threads; i++)
        pthread_join(threads[i].id, NULL);

    print_thrs_balances(bank, threads, opt.num_threads);
    print_acc_balances(bank, threads, opt.num_threads);

    for (int i = 0; i < opt.num_threads; i++)
        free(threads[i].args);

    free(threads);
}

// allocate memory, and set all accounts to 0
void init_accounts(struct bank *bank, int num_accounts) {
    bank->num_accounts = num_accounts;
    bank->accounts     = malloc(bank->num_accounts * sizeof(int));
    bank->mutex = malloc(bank->num_accounts * sizeof(pthread_mutex_t));

    for(int i=0; i < bank->num_accounts; i++){
        bank->accounts[i] = 0;
        pthread_mutex_init(&bank->mutex[i],NULL);
      }
}

int main (int argc, char **argv)
{
    struct options      opt;
    struct bank         bank;
    struct thread_info *thrs;
    struct thread_info thr;

    srand(time(NULL));

    // Default values for the options
    opt.num_threads  = 5;
    opt.num_accounts = 10;
    opt.iterations   = 100;
    opt.delay        = 10;

    read_options(argc, argv, &opt);

    init_accounts(&bank, opt.num_accounts);

    thrs = start_threads(opt, &bank, deposit);
    wait(opt, &bank, thrs);

    thrs = start_threads(opt, &bank, transfer);
    thr = start_thread(opt,&bank,print_total_balance);

    for (int i = 0; i < opt.num_threads; i++)
        pthread_join(thrs[i].id, NULL);

    pthread_cancel(thr.id);
    unlock_all(bank.mutex,opt.num_accounts);
    printf("\x1b[0m");
    free(thr.args);

    print_acc_balances(&bank, thrs, opt.num_threads);
    for (int i = 0; i < opt.num_threads; i++)
        free(thrs[i].args);
    free(thrs);


    free(bank.mutex);
    free(bank.accounts);
    return 0;
}
