#ifndef LST_TIMER_H
#define LST_TIMER_H


#include <time.h>
#include <stdio.h>

class util_timer;
struct client_data
{
    sockaddr_in address;
    int sockfd;
    util_timer *timer;
};

class util_timer
{
    public:
        util_timer() : prev(NULL), next(NULL){}
    public:
        time_t expire;
        void (*cb_func)(client_data *);
        client_data *user_data;
        util_timer *next;
        util_timer *prev;
};
//基于升序链表的定时器
class sort_timer_list
{
    public:
        sort_timer_list() : head(NULL), tail(NULL){}
        ~sort_timer_list()
        {
            util_timer *temp = head;
            while(temp)
            {
                head = temp->next;
                delete temp;
                temp = head;
            }
        }
        void add_timer(util_timer *timer)
        {
            if(!timer)return;
            if(!head)
            {
                head = tail = timer;
                return;
            }
            if( timer->expire < head->expire )
            {
                timer->next = head;
                head->prev = timer;
                head = timer;
                return ;
            }
            add_timer( timer, head );
        }
        //当某个定时任务发生变化时，调整对应的定时器
        //在链表中的位置。这个函数只考虑被调整的定时器
        //的超时时间延长情况，即该定时器需要往链表尾部移动
        void adjust_timer(util_timer *timer)
        {
            if(!timer)return;
            util_timer *temp = timer->next;
            if(!temp || (timer->expire < temp->expire))
                return;
            if( timer == head )
            {
                head = head->next;
                head->prev = NULL;
                timer->next = NULL;
                add_timer( timer, head );
            }
            else
            {
                timer->prev->next = timer->next;
                timer->next->prev = timer->prev;
                add_timer(timer, timer->next);
            }
        }
        void del_timer( util_timer *timer )
        {
            if(!timer)return;
            if( ( timer == head ) && (timer == tail) )
            {
                delete timer;
                head = NULL;
                tail = NULL;
                return;
            }
            if (timer == head)
            {
                head = head->next;
                head->prev = NULL;
                delete timer;
                return;
            }
            if( timer == tail)
            {
                tail = tail->prev;
                tail->next = NULL;
                delete timer;
                return;
            }
            timer->prev->next = timer->next;
            timer->next->prev = timer->prev;
            delete timer;
        }
        void tick()
        {
            if(!head)
            {
                return;
            }
            printf("timer tick\n");
            time_t cur = time( NULL );
            util_timer *tmp = head;
            while( tmp )
            {
                if( cur < tmp->expire )
                {
                    break;
                }
                tmp->cb_func(tmp->user_data);
                head = tmp->next;
                if(head)
                {
                    head->prev = NULL;
                }
                delete tmp;
                tmp = head;
            }
        } 
    private:
        void add_timer(util_timer *timer, util_timer *lst_head)
        {
            util_timer *prev = lst_head;
            util_timer *tmp = prev->next;
            while(tmp)
            {
                if(timer->expire < tmp -> expire)
                {
                    prev->next = timer;
                    timer->next = tmp;
                    tmp->prev = timer;
                    timer->prev = prev;
                    break;
                }
                prev = tmp;
                tmp = tmp->next;
            }
            if(!tmp)
            {
                prev->next = timer;
                timer->prev = prev;
                timer->next = NULL;
                tail = timer;
            }
        }


    private:
        util_timer *head;
        util_timer *tail;
};

#endif // LST_TIMER_H