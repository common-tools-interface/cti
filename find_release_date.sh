#! /bin/bash

today=$(date +%d)
today=$(echo $today | sed 's/^0//')
if [[ $today -gt 7 ]]
then
  month_year=$(date -d"$(date +%Y-%m-01) +1 month" +%Y-%m)
else
  month_year=$(date -d"$(date +%Y-%m-01)" +%Y-%m)
fi

first_day=$(date -d "$month_year-01" +%u)

if [[ $first_day -eq 4 ]]
then
  day=1
elif [[ $first_day -eq 7 ]]
then
  day=5
elif [[ $first_day -lt 4 ]]
then
  day=$(expr 4 - $first_day + 1 )
else
  day=$(expr 4 + 7 - $first_day  + 1)
fi
day=0$day

date -d"$month_year-$day" "+%B %d, %Y"


