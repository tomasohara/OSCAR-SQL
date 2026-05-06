# How to cherry-pick a commit from upstream 

## Step-by-Step Instructions

```bash

```

**1. Fetch the commits from the remote repository**

Download the commits from the other repository without merging them:

```bash
git fetch upstream
```

**2. Find the commit hash you want to cherry-pick**

You can browse the fetched commits to find the one you need:

```bash
git log upstream/master
```

Copy the commit hash (the long string of characters).

**3. Cherry-pick the commit**

Apply the specific commit to your current branch:

```bash
git cherry-pick <commit-hash>
```

For example: `git cherry-pick a1b2c3d4e5f6`

**4. Resolve any conflicts (if they occur)**

If there are conflicts, Git will pause and let you fix them. Edit the conflicting files, then:

bash

```bash
git add <fixed-files>
git cherry-pick --continue
```

**5. Merge to my repository**

```bash
git push origin master
```

