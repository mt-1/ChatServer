CREATE TABLE user(
	id INT PRIMARY KEY AUTO_INCREMENT COMMENT '用户id',
	name VARCHAR(50) NOT NULL UNIQUE COMMENT '用户名',
	password VARCHAR(50) NOT NULL COMMENT '用户密码',
	state ENUM('online', 'offline') DEFAULT 'offline' COMMENT '当前登录状态'
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE friend(
	userid INT NOT NULL,
	friendid INT NOT NULL,
	PRIMARY KEY (userid, friendid)
);


CREATE TABLE ALLGroup(
	id INT PRIMARY KEY AUTO_INCREMENT COMMENT '组id',
	groupname VARCHAR(50) NOT NULL COMMENT '组名',
	groupdesc VARCHAR(200) DEFAULT '' COMMENT '组功能描述'
);

CREATE TABLE GroupUser(
	groupid INT PRIMARY KEY COMMENT '组id',
	userid INT NOT NULL COMMENT '组员id',
	grouprole ENUM('creator', 'normal') DEFAULT 'normal' COMMENT '组内角色'
);

CREATE TABLE offlineMessage(
	userid INT PRIMARY KEY,
	message VARCHAR(500) NOT NULL COMMENT '离线消息(存储json字符串)'
);
