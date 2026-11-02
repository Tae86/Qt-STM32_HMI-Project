# Database (SQLite)

Qt HMI가 사용하는 로컬 SQLite DB 문서입니다. 테이블은 앱 실행 시 자동 생성됩니다.

- [DATABASE.md](DATABASE.md) — 테이블 설계, 기록 정책 (팀원 작성 문서)
- [schema.sql](schema.sql) — 스키마 DDL (`qt/databasemanager.cpp`와 동일)
- 코드: [`qt/databasemanager.cpp`](../qt/databasemanager.cpp)

```bash
sqlite3 hmi.db < schema.sql   # 직접 확인용
```
