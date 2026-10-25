# Run the cppgram live check on Heroku

This bundle deploys the project as a Heroku `worker` container. It is a one-off
connectivity test, not a web service; it does not listen on `$PORT`.

## 1. Protect credentials

The bot token and API hash were exposed in chat/log output. Revoke the old bot
token with BotFather and use a newly issued token. Replace the API hash too if
Telegram's app settings allow it. Never put credential values in this repository,
Dockerfile, `heroku.yml`, or a log you share.

## 2. Deploy

The `heroku.yml` manifest lets Heroku build the Docker image remotely; you do not
need Docker installed on your phone. You do need the Heroku CLI and Git, or a
GitHub repository connected to Heroku.
Heroku documents this `heroku.yml` Docker-build route for Cedar-generation apps.

From this `cppgram` directory, using the Heroku CLI:

```bash
git init -b main
git add .
git commit -m "Prepare cppgram Heroku live check"
heroku login
heroku create --stack container
git push heroku main
```

If you already created the Heroku app in the dashboard, set its stack and connect
the Git remote instead:

```bash
heroku stack:set container --app APP_NAME
heroku git:remote --app APP_NAME
git push heroku main
```

### Deploying from the Heroku dashboard

Heroku does not deploy a raw ZIP directly. Put the contents of this `cppgram`
folder at the root of a GitHub repository, create a Heroku app, set its stack to
`container`, and connect that repository from the app's **Deploy → GitHub** page.
Deploy the branch containing `heroku.yml` and `Dockerfile`.

## 3. Add runtime configuration

In the Heroku dashboard, open the app's **Settings → Config Vars** and add:

- `TG_API_ID`
- `TG_API_HASH`
- `TG_BOT_TOKEN`

Enter fresh values there; do not paste them into chat or commit them to Git.

## 4. Run the one-off test

```bash
heroku run --app APP_NAME --type=worker /app/cppgram_live_check
```

The dyno filesystem is temporary. The check writes `live-dcN.session` files in
`/tmp`; do not share those files. One-off dyno time is billed by Heroku.

Share only a sanitized `FAIL stage=...` line if the test fails. Remove bot tokens,
API hashes, account IDs, usernames, and session files from any output first.


<p align="center">
  <a href="https://dashboard.heroku.com/new?template=https://github.com/Badxd/cppgram"><img src="https://img.shields.io/badge/Deploy%20to-Heroku-430098?style=for-the-badge&logo=heroku&logoColor=white" alt="Deploy to Heroku"></a>
  
