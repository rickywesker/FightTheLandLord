// 斗地主（FightTheLandlord）样例程序
// 最后更新于2018-5-26
// 作者：zhouhy/Team CCL
// 游戏信息：http://www.botzone.org/games#FightTheLandlord
#include <iostream>
#include <set>
#include <string>
#include <cassert>
#include <cstring> // 注意memset是cstring里的
#include <algorithm>
#include "jsoncpp/json.h" // 在平台上，C++编译时默认包含此库
#include <vector>
using namespace std;
constexpr int PLAYER_COUNT = 3;

enum class CardComboType
{
    PASS, // 过
    SINGLE, // 单张
    PAIR, // 对子
    STRAIGHT, // 顺子
    STRAIGHT2, // 双顺
    TRIPLET, // 三条
    TRIPLET1, // 三带一
    TRIPLET2, // 三带二
    BOMB, // 炸弹
    QUADRUPLE2, // 四带二（只）
    QUADRUPLE4, // 四带二（对）
    PLANE, // 飞机
    PLANE1, // 飞机带小翼
    PLANE2, // 飞机带大翼
    SSHUTTLE, // 航天飞机
    SSHUTTLE2, // 航天飞机带小翼
    SSHUTTLE4, // 航天飞机带大翼
    ROCKET, // 火箭
    INVALID // 非法牌型
};

int cardComboScores[] = {
        0, // 过
        1, // 单张
        2, // 对子
        6, // 顺子
        6, // 双顺
        4, // 三条
        4, // 三带一
        4, // 三带二
        10, // 炸弹
        8, // 四带二（只）
        8, // 四带二（对）
        8, // 飞机
        8, // 飞机带小翼
        8, // 飞机带大翼
        10, // 航天飞机（需要特判：二连为10分，多连为20分）
        10, // 航天飞机带小翼
        10, // 航天飞机带大翼
        16, // 火箭
        0 // 非法牌型
};

template <typename CARD_ITERATOR>
CARD_ITERATOR plusplus(CARD_ITERATOR s,int n)
{
    for(int i=1;i<=n;i++)
        s++;
    return s;
}

#ifndef _BOTZONE_ONLINE
string cardComboStrings[] = {
        "PASS",
        "SINGLE",
        "PAIR",
        "STRAIGHT",
        "STRAIGHT2",
        "TRIPLET",
        "TRIPLET1",
        "TRIPLET2",
        "BOMB",
        "QUADRUPLE2",
        "QUADRUPLE4",
        "PLANE",
        "PLANE1",
        "PLANE2",
        "SSHUTTLE",
        "SSHUTTLE2",
        "SSHUTTLE4",
        "ROCKET",
        "INVALID"
};
#endif

// 用0~53这54个整数表示唯一的一张牌
using Card = short;
constexpr Card card_joker = 52;
constexpr Card card_JOKER = 53;

// 我的牌有哪些
set<Card> myCards;

// 地主被明示的牌有哪些
set<Card> landlordPublicCards;

// 大家从最开始到现在都出过什么
vector<vector<Card>> whatTheyPlayed[PLAYER_COUNT];


// 大家还剩多少牌
short cardRemaining[PLAYER_COUNT] = { 20, 17, 17 };

// 我是几号玩家（0-地主，1-农民甲，2-农民乙）
int myPosition;

// 除了用0~53这54个整数表示唯一的牌，
// 这里还用另一种序号表示牌的大小（不管花色），以便比较，称作等级（Level）
// 对应关系如下：
// 3 4 5 6 7 8 9 10	J Q K	A	2	小王	大王
// 0 1 2 3 4 5 6 7	8 9 10	11	12	13	14
using Level = short;
constexpr Level MAX_LEVEL = 15;
constexpr Level MAX_STRAIGHT_LEVEL = 11;
constexpr Level level_joker = 13;
constexpr Level level_JOKER = 14;
int passCnt=0;
/**
 * 将Card变成Level
 */
constexpr Level card2level(Card card)
{
    return card / 4 + card / 53;
}

// 牌的组合，用于计算牌型
struct CardCombo
{
    // 表示同等级的牌有多少张
    // 会按个数从大到小、等级从大到小排序
    struct CardPack
    {
        Level level;
        short count;

        bool operator< (const CardPack& b) const
        {
            if (count == b.count)
                return level > b.level;
            return count > b.count;
        }
    };
    vector<Card> cards; // 原始的牌，未排序
    vector<CardPack> packs; // 按数目和大小排序的牌种
    CardComboType comboType; // 算出的牌型
    Level comboLevel = 0; // 算出的大小序

    /**
     * 检查个数最多的CardPack递减了几个
     */
    int findMaxSeq() const
    {
        for (unsigned c = 1; c < packs.size(); c++)
            if (packs[c].count != packs[0].count ||
                packs[c].level != packs[c - 1].level - 1)
                return c;
        return packs.size();
    }

    /**
     * 这个牌型最后算总分的时候的权重
     */
    int getWeight() const
    {
        if (comboType == CardComboType::SSHUTTLE ||
            comboType == CardComboType::SSHUTTLE2 ||
            comboType == CardComboType::SSHUTTLE4)
            return cardComboScores[(int)comboType] + (findMaxSeq() > 2) * 10;
        return cardComboScores[(int)comboType];
    }

    // 创建一个空牌组
    CardCombo() : comboType(CardComboType::PASS) {}

    /**
     * 通过Card（即short）类型的迭代器创建一个牌型
     * 并计算出牌型和大小序等
     * 假设输入没有重复数字（即重复的Card）
     */
    template <typename CARD_ITERATOR>
    CardCombo(CARD_ITERATOR begin, CARD_ITERATOR end)
    {
        // 特判：空
        if (begin == end)
        {
            comboType = CardComboType::PASS;
            return;
        }

        // 每种牌有多少个
        short counts[MAX_LEVEL + 1] = {};

        // 同种牌的张数（有多少个单张、对子、三条、四条）
        short countOfCount[5] = {};

        cards = vector<Card>(begin, end);
        for (Card c : cards)
            counts[card2level(c)]++;
        for (Level l = 0; l <= MAX_LEVEL; l++)
            if (counts[l])
            {
                packs.push_back(CardPack{ l, counts[l] });
                countOfCount[counts[l]]++;
            }
        sort(packs.begin(), packs.end());

        // 用最多的那种牌总是可以比较大小的
        comboLevel = packs[0].level;

        // 计算牌型
        // 按照 同种牌的张数 有几种 进行分类
        vector<int> kindOfCountOfCount;//我的牌有多少種呀？
        for (int i = 0; i <= 4; i++)
            if (countOfCount[i])
                kindOfCountOfCount.push_back(i);
        sort(kindOfCountOfCount.begin(), kindOfCountOfCount.end());

        int curr, lesser;

        switch (kindOfCountOfCount.size())
        {
            case 1: // 只有一类牌
                curr = countOfCount[kindOfCountOfCount[0]];
                switch (kindOfCountOfCount[0])
                {
                    case 1:
                        // 只有若干单张
                        if (curr == 1)
                        {
                            comboType = CardComboType::SINGLE;
                            return;
                        }
                        if (curr == 2 && packs[1].level == level_joker)
                        {
                            comboType = CardComboType::ROCKET;
                            return;
                        }
                        if (curr >= 5 && findMaxSeq() == curr &&
                            packs.begin()->level <= MAX_STRAIGHT_LEVEL)
                        {
                            comboType = CardComboType::STRAIGHT;
                            return;
                        }
                        break;
                    case 2:
                        // 只有若干对子
                        if (curr == 1)
                        {
                            comboType = CardComboType::PAIR;
                            return;
                        }
                        if (curr >= 3 && findMaxSeq() == curr &&
                            packs.begin()->level <= MAX_STRAIGHT_LEVEL)
                        {
                            comboType = CardComboType::STRAIGHT2;
                            return;
                        }
                        break;
                    case 3:
                        // 只有若干三条
                        if (curr == 1)
                        {
                            comboType = CardComboType::TRIPLET;
                            return;
                        }
                        if (findMaxSeq() == curr &&
                            packs.begin()->level <= MAX_STRAIGHT_LEVEL)
                        {
                            comboType = CardComboType::PLANE;
                            return;
                        }
                        break;
                    case 4:
                        // 只有若干四条
                        if (curr == 1)
                        {
                            comboType = CardComboType::BOMB;
                            return;
                        }
                        if (findMaxSeq() == curr &&
                            packs.begin()->level <= MAX_STRAIGHT_LEVEL)
                        {
                            comboType = CardComboType::SSHUTTLE;
                            return;
                        }
                }
                break;
            case 2: // 有两类牌
                curr = countOfCount[kindOfCountOfCount[1]];
                lesser = countOfCount[kindOfCountOfCount[0]];
                if (kindOfCountOfCount[1] == 3)
                {
                    // 三条带？
                    if (kindOfCountOfCount[0] == 1)
                    {
                        // 三带一
                        if (curr == 1 && lesser == 1)
                        {
                            comboType = CardComboType::TRIPLET1;
                            return;
                        }
                        if (findMaxSeq() == curr && lesser == curr &&
                            packs.begin()->level <= MAX_STRAIGHT_LEVEL)
                        {
                            comboType = CardComboType::PLANE1;
                            return;
                        }
                    }
                    if (kindOfCountOfCount[0] == 2)
                    {
                        // 三带二
                        if (curr == 1 && lesser == 1)
                        {
                            comboType = CardComboType::TRIPLET2;
                            return;
                        }
                        if (findMaxSeq() == curr && lesser == curr &&
                            packs.begin()->level <= MAX_STRAIGHT_LEVEL)
                        {
                            comboType = CardComboType::PLANE2;
                            return;
                        }
                    }
                }
                if (kindOfCountOfCount[1] == 4)
                {
                    // 四条带？
                    if (kindOfCountOfCount[0] == 1)
                    {
                        // 四条带两只 * n
                        if (curr == 1 && lesser == 2)
                        {
                            comboType = CardComboType::QUADRUPLE2;
                            return;
                        }
                        if (findMaxSeq() == curr && lesser == curr * 2 &&
                            packs.begin()->level <= MAX_STRAIGHT_LEVEL)
                        {
                            comboType = CardComboType::SSHUTTLE2;
                            return;
                        }
                    }
                    if (kindOfCountOfCount[0] == 2)
                    {
                        // 四条带两对 * n
                        if (curr == 1 && lesser == 2)
                        {
                            comboType = CardComboType::QUADRUPLE4;
                            return;
                        }
                        if (findMaxSeq() == curr && lesser == curr * 2 &&
                            packs.begin()->level <= MAX_STRAIGHT_LEVEL)
                        {
                            comboType = CardComboType::SSHUTTLE4;
                            return;
                        }
                    }
                }
        }

        comboType = CardComboType::INVALID;
    }

    /**
     * 判断指定牌组能否大过当前牌组（这个函数不考虑过牌的情况！）
     */
    bool canBeBeatenBy(const CardCombo& b) const
    {
        if (comboType == CardComboType::INVALID || b.comboType == CardComboType::INVALID)
            return false;
        if (b.comboType == CardComboType::ROCKET)
            return true;
        if (b.comboType == CardComboType::BOMB)
            switch (comboType)
            {
                case CardComboType::ROCKET:
                    return false;
                case CardComboType::BOMB:
                    return b.comboLevel > comboLevel;
                default:
                    return true;
            }
        return b.comboType == comboType && b.cards.size() == cards.size() && b.comboLevel > comboLevel;
    }

    /**
     * 从指定手牌中寻找第一个能大过当前牌组的牌组
     * 如果随便出的话只出第一张
     * 如果不存在则返回一个PASS的牌组
     */
    template <typename CARD_ITERATOR>
    CardCombo findFirstValid(CARD_ITERATOR begin, CARD_ITERATOR end) const
    {
        if (comboType == CardComboType::PASS) // 如果不需要大过谁，只需要随便出
        {
            //            CARD_ITERATOR second = begin;
            //            second++;
            //            CardCombo temp3(begin,second);
            //            second++;
            //
            //            CardCombo temp(begin,second);
            //            if(temp.comboType==CardComboType::PAIR)
            //            {
            //                second++;
            //                CardCombo temp2(begin,second);
            //                if(temp2.comboType==CardComboType::TRIPLET)
            //                    return temp2;
            //                else
            //                    return temp;
            //            }
            //            else
            //                return temp3;
            //            // 那么就出第一张牌……
            CardCombo MyCombo(begin,end);

            auto MyDeck = vector<Card>(begin, end); //
            short MyCounts[MAX_LEVEL + 1] = {};
            unsigned short MyKindCount = 0;
            // 先数一下手牌里每种牌有多少个
            for (Card c : MyDeck)
                MyCounts[card2level(c)]++;
            // 再数一下手牌里有多少种牌
            for (short c : MyCounts)
                if (c)
                    MyKindCount++;


            // 同种牌的张数（有多少个单张、对子、三条、四条）
            short MyCountOfCount[5] = {};
            for (Level l = 0; l <= MAX_LEVEL; l++) {
                if (MyCounts[l]) {
                    MyCountOfCount[MyCounts[l]]++;
                }
            }
            //By ricky 如果手牌数目小于6直接出顺子
            //if(MyDeck.size()<6)
            //如果有三代
            if(MyCountOfCount[3])
            {
                vector<Card> solve;

                if(MyCountOfCount[3]<2)//只有一个三带
                {
                    CARD_ITERATOR beginIT = begin;
                    for(int i=0;i<MAX_LEVEL-3;i++)
                    {
                        if(MyCounts[i]==3)
                            break;
                        beginIT=plusplus(beginIT, MyCounts[i]);
                    }
                    for(int i=0;i<3;i++)
                    {
                        solve.push_back(*beginIT);
                        beginIT++;
                    }
                    //三代一
                    if(MyCountOfCount[1])
                    {
                        CARD_ITERATOR beginIT_1 = begin;
                        for(int i=0;i<MAX_LEVEL-3;i++)
                        {
                            if(MyCounts[i]==1) break;
                            beginIT_1=plusplus(beginIT_1,MyCounts[i]);
                        }
                        solve.push_back(*beginIT_1);
                        return CardCombo(solve.begin(),solve.end());
                    }
                    //三代二
                    if(MyCountOfCount[2])
                    {
                        CARD_ITERATOR beginIT_2 = begin;
                        for(int i=0;i<MAX_LEVEL-3;i++)
                        {
                            if(MyCounts[i]==2) break;
                            beginIT_2=plusplus(beginIT_2,MyCounts[i]);
                        }
                        solve.push_back(*beginIT_2);
                        beginIT_2++;
                        solve.push_back(*beginIT_2);
                        return CardCombo(solve.begin(),solve.end());
                    }
                    if(solve.size()!=0)
                        return CardCombo(solve.begin(),solve.end());
                }
                else
                {
                    int tmp_cards=0;
                    CARD_ITERATOR beginIT = begin;
                    for(int i = 0;i<MAX_LEVEL-3;++i)
                    {

                        if(MyCounts[i]!=3)
                            beginIT=plusplus(beginIT,MyCounts[i]);
                        else
                        {
                            int tmp_max=1;
                            for(int j=i+1;j<MAX_LEVEL-3;++j){
                                if(MyCounts[j]==3)
                                    tmp_max++;
                                else
                                    break;
                            }
                            if(tmp_max>=2)
                            {


                                for(int i=0;i<6;i++)
                                {
                                    solve.push_back(*beginIT);
                                    beginIT++;
                                }
                                //三代一
                                if(MyCountOfCount[1]>=2)
                                {
                                    int num=0;
                                    CARD_ITERATOR beginIT_1 = begin;
                                    for(int i=0;i<MAX_LEVEL-3;i++)
                                    {
                                        if(MyCounts[i]==1) {
                                            num++;
                                            solve.push_back(*beginIT_1);
                                        }
                                        if(num==2)break;
                                        beginIT_1=plusplus(beginIT_1,MyCounts[i]);
                                    }
                                    return CardCombo(solve.begin(),solve.end());
                                }
                                //三代二
                                if(MyCountOfCount[2])
                                {
                                    CARD_ITERATOR beginIT_2 = begin;
                                    for(int i=0;i<MAX_LEVEL-3;i++)
                                    {
                                        if(MyCounts[i]==2) break;
                                        beginIT_2=plusplus(beginIT_2,MyCounts[i]);
                                    }
                                    solve.push_back(*beginIT_2);
                                    beginIT_2++;
                                    solve.push_back(*beginIT_2);
                                    return CardCombo(solve.begin(),solve.end());
                                }
                                if(solve.size()!=0)
                                    return CardCombo(solve.begin(),solve.end());


                            }
                            else
                            {
                                CARD_ITERATOR beginIT = begin;
                                for(int i=0;i<MAX_LEVEL-3;i++)
                                {
                                    if(MyCounts[i]==3)
                                        break;
                                    beginIT=plusplus(beginIT, MyCounts[i]);
                                }
                                for(int i=0;i<3;i++)
                                {
                                    solve.push_back(*beginIT);
                                    beginIT++;
                                }
                                //三代一
                                if(MyCountOfCount[1])
                                {
                                    CARD_ITERATOR beginIT_1 = begin;
                                    for(int i=0;i<MAX_LEVEL-3;i++)
                                    {
                                        if(MyCounts[i]==1) break;
                                        beginIT_1=plusplus(beginIT_1,MyCounts[i]);
                                    }
                                    solve.push_back(*beginIT_1);
                                    return CardCombo(solve.begin(),solve.end());
                                }
                                //三代二
                                if(MyCountOfCount[2])
                                {
                                    CARD_ITERATOR beginIT_2 = begin;
                                    for(int i=0;i<MAX_LEVEL-3;i++)
                                    {
                                        if(MyCounts[i]==2) break;
                                        beginIT_2=plusplus(beginIT_2,MyCounts[i]);
                                    }
                                    solve.push_back(*beginIT_2);
                                    beginIT_2++;
                                    solve.push_back(*beginIT_2);
                                    return CardCombo(solve.begin(),solve.end());
                                }
                                if(solve.size()!=0)
                                    return CardCombo(solve.begin(),solve.end());
                            }
                            tmp_cards+=MyCounts[i];
                        }

                    }

                }
            }
            //如果有单牌
            if(MyCountOfCount[1]){
                CARD_ITERATOR beginIT = begin;

                if(MyCountOfCount[1]<5||MyCombo.findMaxSeq()<5||MyCountOfCount[1]<MyCombo.findMaxSeq()){//单张<5
                    bool flag=false;
                    for(int i  = 0;i < MAX_LEVEL;++i)
                    {
                        if(MyCounts[i]==1){break;}
                        int _i = MyCounts[i];
                        while(_i--){
                            beginIT++;
                        }
                        if(MyDeck.size()>8 && i>8)
                        {
                            flag=true;
                            break;
                        }


                    }
                    if(!flag){
                        CARD_ITERATOR tmpIT = beginIT;
                        tmpIT++;
                        return CardCombo(beginIT,tmpIT);
                    }
                }
                else{
                    int tmp_cards=0;
                    for(int i = 0;i<MAX_LEVEL-8;++i){
                        tmp_cards+=MyCounts[i];
                        if(MyCounts[i]==1){
                            int tmp_max=1;
                            for(int j=i+1;j<MAX_LEVEL-3;++j){
                                if(MyCounts[j]==1)
                                    tmp_max++;
                                else
                                    break;
                            }
                            if(tmp_max>=5){
                                //>5出顺子
                                CARD_ITERATOR tmpBegin=plusplus(begin, tmp_cards);
                                CARD_ITERATOR tmpEnd=plusplus(tmpBegin, tmp_max);
                                return CardCombo(tmpBegin,tmpEnd);
                            }
                            else
                            {
                                //否则出第一张最小的单牌
                                CARD_ITERATOR tmpBegin=plusplus(begin, tmp_cards);
                                CARD_ITERATOR second=tmpBegin;
                                second++;
                                return CardCombo(tmpBegin,second);
                            }
                        }
                    }
                }
            }

            //如果有对子
            if(MyCountOfCount[2])
            {
                CARD_ITERATOR beginIT = begin;
                if(MyCountOfCount[2]<3)   //如果没有连对
                {
                    for(int i  = 0;i < MAX_LEVEL;++i)
                    {
                        if(MyCounts[i]==2){break;}
                        beginIT = plusplus(beginIT, MyCounts[i]);
                    }
                    CARD_ITERATOR tmpIT = beginIT;
                    tmpIT++;
                    tmpIT++;
                    return CardCombo(beginIT,tmpIT);
                }
                else
                {
                    int tmp_cards=0;
                    for(int i = 0;i<MAX_LEVEL-8;++i){

                        if(MyCounts[i]==2){
                            int tmp_max=1;
                            for(int j=i+1;j<MAX_LEVEL-3;++j){
                                if(MyCounts[j]==2)
                                    tmp_max++;
                                else
                                    break;
                            }
                            if(tmp_max>=3){
                                CARD_ITERATOR tmpBegin=plusplus(begin, tmp_cards);
                                CARD_ITERATOR tmpEnd=plusplus(tmpBegin, tmp_max*2);
                                return CardCombo(tmpBegin,tmpEnd);
                            }
                            else
                            {
                                CARD_ITERATOR tmpBegin=plusplus(begin, tmp_cards);
                                CARD_ITERATOR second=tmpBegin;
                                second++;
                                second++;
                                return CardCombo(tmpBegin,second);
                            }
                        }
                        tmp_cards+=MyCounts[i];
                    }
                }
            }
            else
            {
                CARD_ITERATOR second=begin;
                second++;
                return CardCombo(begin,second);
            }

        }

        // 然后先看一下是不是火箭，是的话就过
        if (comboType == CardComboType::ROCKET)
            return CardCombo();

        // 现在打算从手牌中凑出同牌型的牌
        auto deck = vector<Card>(begin, end); // 手牌
        short counts[MAX_LEVEL + 1] = {};

        unsigned short kindCount = 0;

        // 先数一下手牌里每种牌有多少个
        for (Card c : deck)
            counts[card2level(c)]++;

        // 手牌如果不够用，直接不用凑了，看看能不能炸吧
        if (deck.size() < cards.size())//如果手牌数目小于对方出牌数目
            goto failure;

        // 再数一下手牌里有多少种牌
        for (short c : counts)
            if (c)
                kindCount++;
        if(comboType== CardComboType::SINGLE)
        {
            CARD_ITERATOR begin_s=begin;
            for(int i=0;i<MAX_LEVEL;i++)
            {
                if(i>packs[0].level && counts[i]==1)
                {
                    if(i==13 && counts[13]==1 && counts[14]==1)
                    {
                        return CardCombo();
                    }
                    else
                    {
                        CARD_ITERATOR second_s=begin_s;
                        second_s++;
                        return CardCombo(begin_s,second_s);
                    }
                }
                begin_s=plusplus(begin_s,counts[i]);

            }

            CARD_ITERATOR begin_d=begin;
            for(int i=0;i<MAX_LEVEL;i++)
            {
                if(i>packs[0].level && counts[i]!=4 && counts[i]!=0)

                {
                    CARD_ITERATOR end_d=begin_d;
                    end_d++;
                    return CardCombo(begin_d,end_d);
                }
                begin_d=plusplus(begin_d, counts[i]);
            }

            return CardCombo();
        }
        // 否则不断增大当前牌组的主牌，看看能不能找到匹配的牌组

        {
            // 开始增大主牌
            int mainPackCount = findMaxSeq();
            bool isSequential =
                    comboType == CardComboType::STRAIGHT ||
                    comboType == CardComboType::STRAIGHT2 ||
                    comboType == CardComboType::PLANE ||
                    comboType == CardComboType::PLANE1 ||
                    comboType == CardComboType::PLANE2 ||
                    comboType == CardComboType::SSHUTTLE ||
                    comboType == CardComboType::SSHUTTLE2 ||
                    comboType == CardComboType::SSHUTTLE4;
            for (Level i = 1; ; i++) // 增大多少
            {
                for (int j = 0; j < mainPackCount; j++)
                {
                    int level = packs[j].level + i;

                    // 各种连续牌型的主牌不能到2，非连续牌型的主牌不能到小王，单张的主牌不能超过大王
                    if ((comboType == CardComboType::SINGLE && level > MAX_LEVEL) ||
                        (isSequential && level > MAX_STRAIGHT_LEVEL) ||
                        (comboType != CardComboType::SINGLE && !isSequential && level >= level_joker))
                        goto failure;

                    // 如果手牌中这种牌不够，就不用继续增了
                    if (counts[level] < packs[j].count)
                        goto next;
                }

                {
                    // 找到了合适的主牌，那么从牌呢？
                    // 如果手牌的种类数不够，那从牌的种类数就不够，也不行
                    if (kindCount < packs.size())
                        continue;

                    // 好终于可以了
                    // 计算每种牌的要求数目吧
                    short requiredCounts[MAX_LEVEL + 1] = {};
                    for (int j = 0; j < mainPackCount; j++)
                        requiredCounts[packs[j].level + i] = packs[j].count;
                    for (unsigned j = mainPackCount; j < packs.size(); j++)
                    {
                        Level k;
                        for (k = 0; k <= MAX_LEVEL; k++)
                        {
                            if (requiredCounts[k] || counts[k] < packs[j].count)
                                continue;
                            requiredCounts[k] = packs[j].count;
                            break;
                        }
                        if (k == MAX_LEVEL + 1) // 如果是都不符合要求……就不行了
                            goto next;
                    }


                    // 开始产生解
                    vector<Card> solve;
                    for (Card c : deck)
                    {
                        Level level = card2level(c);
                        if (requiredCounts[level])
                        {
                            solve.push_back(c);
                            requiredCounts[level]--;
                        }
                    }
                    return CardCombo(solve.begin(), solve.end());
                }

                next:
                ; // 再增大
            }
        }

        failure:
        // 实在找不到啊
        // 最后看一下能不能炸吧

        for (Level i = 0; i < level_joker; i++)
            if (counts[i] == 4 && (comboType != CardComboType::BOMB || i > packs[0].level)) // 如果对方是炸弹，能炸的过才行
            {
                // 还真可以啊……
                Card bomb[] = { Card(i * 4), Card(i * 4 + 1), Card(i * 4 + 2), Card(i * 4 + 3) };
                return CardCombo(bomb, bomb + 4);
            }

        // 有没有火箭？
        if (counts[level_joker] + counts[level_JOKER] == 2)
        {
            Card rocket[] = { card_joker, card_JOKER };
            return CardCombo(rocket, rocket + 2);
        }

        // ……
        return CardCombo();
    }

    void debugPrint()
    {
#ifndef _BOTZONE_ONLINE
        std::cout << "【" << cardComboStrings[(int)comboType] <<
                  "共" << cards.size() << "张，大小序" << comboLevel << "】";
#endif
    }
};


// 当前要出的牌需要大过谁
CardCombo lastValidCombo;


namespace BotzoneIO
{
    using namespace std;
    void input()
    {
        // 读入输入（平台上的输入是单行）
        string line;
        getline(cin, line);
        Json::Value input;
        Json::Reader reader;
        reader.parse(line, input);

        // 首先处理第一回合，得知自己是谁、有哪些牌
        {
            auto firstRequest = input["requests"][0u]; // 下标需要是 unsigned，可以通过在数字后面加u来做到
            auto own = firstRequest["own"];
            auto llpublic = firstRequest["public"];
            auto history = firstRequest["history"];
            for (unsigned i = 0; i < own.size(); i++)
                myCards.insert(own[i].asInt());
            for (unsigned i = 0; i < llpublic.size(); i++)
                landlordPublicCards.insert(llpublic[i].asInt());
            if (history[0u].size() == 0)
                if (history[1].size() == 0)
                    myPosition = 0; // 上上家和上家都没出牌，说明是地主
                else
                    myPosition = 1; // 上上家没出牌，但是上家出牌了，说明是农民甲
            else
                myPosition = 2; // 上上家出牌了，说明是农民乙
        }

        // history里第一项（上上家）和第二项（上家）分别是谁的决策
        int whoInHistory[] = { (myPosition - 2 + PLAYER_COUNT) % PLAYER_COUNT, (myPosition - 1 + PLAYER_COUNT) % PLAYER_COUNT };

        int turn = input["requests"].size();
        for (int i = 0; i < turn; i++)
        {
            // 逐次恢复局面到当前
            auto history = input["requests"][i]["history"]; // 每个历史中有上家和上上家出的牌
            int howManyPass = 0;
            for (int p = 0; p < 2; p++)
            {
                int player = whoInHistory[p]; // 是谁出的牌
                auto playerAction = history[p]; // 出的哪些牌
                vector<Card> playedCards;
                for (unsigned _ = 0; _ < playerAction.size(); _++) // 循环枚举这个人出的所有牌
                {
                    int card = playerAction[_].asInt(); // 这里是出的一张牌
                    playedCards.push_back(card);
                }
                whatTheyPlayed[player].push_back(playedCards); // 记录这段历史
                cardRemaining[player] -= playerAction.size();

                if (playerAction.size() == 0)
                    howManyPass++;
                else
                    lastValidCombo = CardCombo(playedCards.begin(), playedCards.end());
            }

            if (howManyPass == 2)
                lastValidCombo = CardCombo();

            if (i < turn - 1)
            {
                // 还要恢复自己曾经出过的牌
                auto playerAction = input["responses"][i]; // 出的哪些牌
                vector<Card> playedCards;
                for (unsigned _ = 0; _ < playerAction.size(); _++) // 循环枚举自己出的所有牌
                {
                    int card = playerAction[_].asInt(); // 这里是自己出的一张牌
                    myCards.erase(card); // 从自己手牌中删掉
                    playedCards.push_back(card);
                }
                whatTheyPlayed[myPosition].push_back(playedCards); // 记录这段历史
                cardRemaining[myPosition] -= playerAction.size();
            }
        }
    }

    /**
     * 输出决策，begin是迭代器起点，end是迭代器终点
     * CARD_ITERATOR是Card（即short）类型的迭代器
     */
    template <typename CARD_ITERATOR>
    void output(CARD_ITERATOR begin, CARD_ITERATOR end)
    {
        Json::Value result, response(Json::arrayValue);
        for (; begin != end; begin++)
            response.append(*begin);
        result["response"] = response;

        Json::FastWriter writer;
        cout << writer.write(result) << endl;
    }
}

int main()
{
    BotzoneIO::input();

    // 做出决策（你只需修改以下部分）

    // findFirstValid 函数可以用作修改的起点
    CardCombo myAction = lastValidCombo.findFirstValid(myCards.begin(), myCards.end());

    // 是合法牌
    assert(myAction.comboType != CardComboType::INVALID);

    assert(
    // 在上家没过牌的时候过牌
            (lastValidCombo.comboType != CardComboType::PASS && myAction.comboType == CardComboType::PASS) ||
            // 在上家没过牌的时候出打得过的牌
            (lastValidCombo.comboType != CardComboType::PASS && lastValidCombo.canBeBeatenBy(myAction)) ||
            // 在上家过牌的时候出合法牌
            (lastValidCombo.comboType == CardComboType::PASS && myAction.comboType != CardComboType::INVALID)
    );

    // 决策结束，输出结果（你只需修改以上部分）

    BotzoneIO::output(myAction.cards.begin(), myAction.cards.end());
}
