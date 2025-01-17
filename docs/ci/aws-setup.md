# AWS Setup for CI Testing

Our CI system needs to test the collector on various AWS instance types to validate hardware-specific features. **We use GitHub Actions to dynamically spin up EC2 instances with specific hardware configurations, run our tests, and then tear down the instances**.

## Overview

The CI system uses two key community actions:
- [machulav/EC2-github-runner](https://github.com/machulav/ec2-github-runner): Manages ephemeral EC2 instances for test execution
- [aws-actions/configure-aws-credentials](https://github.com/aws-actions/configure-aws-credentials): Handles AWS authentication through GitHub's OIDC provider

Each test workflow creates a dedicated GitHub Actions runner on a fresh EC2 instance. This ensures our tests run in clean environments with specific hardware configurations.

## AWS Account Setup

We maintain a dedicated AWS account for CI testing to isolate these resources from production environments. This separation provides clearer cost tracking and stronger security boundaries.

### Administrative Access

After creating the dedicated CI testing account, set up administrative access:

1. In the root account, navigate to IAM Identity Center
2. Create a new user in the IAM Identity Center
3. Create a Permission Set or use the existing "Administrator Access" permission set
   - Note: "Power User Access" is insufficient as it doesn't allow IAM role creation
4. Assign the user to the CI testing account with the Administrator Access permission set

**The Administrator Access permission set is required for subsequent IAM configuration steps**. While Power User Access might seem sufficient, it lacks the necessary permissions for creating IAM roles needed for GitHub Actions integration.

### IAM Configuration

First, configure GitHub as an OIDC provider to enable secure authentication:

1. Open the IAM console
2. Navigate to "Identity Providers" and add a new provider
3. Select "OpenID Connect"
4. Use `https://token.actions.githubusercontent.com` as the provider URL
5. Set the audience to `sts.amazonaws.com`

Next, create an IAM role for GitHub Actions:

1. Create a new role
2. Set the Trusted entity type to Web Identity
3. Select the GitHub OIDC provider as the trust entity
4. Fill in the org (`unvariance`) and repo (`collector`), not filling in the branch
5. Add no permissions (we will do this in a moment)
6. Name the role, e.g., `github-actions-collector`.
7. Verify the generated trusted entities to be:
```json
{
    "Version": "2012-10-17",
    "Statement": [
        {
            "Effect": "Allow",
            "Principal": {
                "Federated": "arn:aws:iam::<ACCOUNT-ID>:oidc-provider/token.actions.githubusercontent.com"
            },
            "Action": "sts:AssumeRoleWithWebIdentity",
            "Condition": {
                "StringEquals": {
                    "token.actions.githubusercontent.com:aud": "sts.amazonaws.com"
                },
                "StringLike": {
                    "token.actions.githubusercontent.com:sub": "repo:unvariance/collector:*"
                }
            }
        }
    ]
}
```

### EC2 Permissions

The IAM role needs permissions to manage EC2 instances. Attach a policy with these minimum permissions:

```json
{
    "Version": "2012-10-17",
    "Statement": [
        {
            "Effect": "Allow",
            "Action": [
                "ec2:RunInstances",
                "ec2:TerminateInstances",
                "ec2:DescribeInstances",
                "ec2:DescribeInstanceStatus"
            ],
            "Resource": "*"
        },
        {
            "Effect": "Allow",
            "Action": "ec2:CreateTags",
            "Resource": "*",
            "Condition": {
                "StringEquals": {
                    "ec2:CreateAction": "RunInstances"
                }
            }
        }
    ]
}
```

## Network Configuration

Create a dedicated VPC for CI testing:

1. Create a new VPC with a single public subnet
2. Set up appropriate security groups to allow:
   - Outbound traffic on port 443 for communication with GitHub

## Repository variables

Configure the repository with the following secrets that can be used in Actions:

- `AWS_ROLE_ARN`: the ARN of the role that allows running and terminating instances
- `AWS_REGION`: the region where we'll run runners
- `AWS_SUBNET_ID`: the subnet ID, needs to be in `AWS_REGION`
- `AWS_SECURITY_GROUP_ID`: the name of the security group that allows runners to pull jobs


## GitHub Workflow Configuration

Here is an example workflow, adapted from the [ec2-github-runner README](https://github.com/machulav/ec2-github-runner?tab=readme-ov-file#example) and [configure-aws-credentials README example](https://github.com/aws-actions/configure-aws-credentials?tab=readme-ov-file#assumerolewithwebidentity-recommended).

```yaml
name: Test EC2 Runner
on: workflow_dispatch  # Manual trigger for testing

jobs:
  start-runner:
    name: Start EC2 runner
    runs-on: ubuntu-latest
    outputs:
      label: ${{ steps.start-ec2-runner.outputs.label }}
      ec2-instance-id: ${{ steps.start-ec2-runner.outputs.ec2-instance-id }}
    steps:
      - name: Configure AWS credentials
        uses: aws-actions/configure-aws-credentials@v4
        with:
          role-to-assume: ${{ secrets.AWS_ROLE_ARN }}
          aws-region: ${{ secrets.AWS_REGION }}
          role-session-name: github-runner-session

      - name: Start EC2 runner
        id: start-ec2-runner
        uses: machulav/ec2-github-runner@v2
        with:
          mode: start
          github-token: ${{ secrets.GITHUB_TOKEN }}
          ec2-image-id: ami-0735c191cf914754d  # Amazon Linux 2 in us-west-2
          ec2-instance-type: t3.micro
          subnet-id: ${{ secrets.AWS_SUBNET_ID }}
          security-group-id: ${{ secrets.AWS_SECURITY_GROUP_ID }}
          aws-resource-tags: >
            [
              {"Key": "Name", "Value": "github-runner"},
              {"Key": "Repository", "Value": "${{ github.repository }}"},
              {"Key": "Workflow", "Value": "${{ github.workflow }}"},
              {"Key": "RunId", "Value": "${{ github.run_id }}"},
              {"Key": "RunNumber", "Value": "${{ github.run_number }}"},
              {"Key": "SHA", "Value": "${{ github.sha }}"},
              {"Key": "Branch", "Value": "${{ github.ref_name }}"},
              {"Key": "Actor", "Value": "${{ github.actor }}"}
            ]

  do-job:
    needs: start-runner
    runs-on: ${{ needs.start-runner.outputs.label }}
    steps:
      - name: Test runner
        run: |
          echo "Hello from EC2 runner!"
          uname -a
          pwd

  stop-runner:
    name: Stop EC2 runner
    needs: [start-runner, do-job]
    runs-on: ubuntu-latest
    if: always()  # Run even if previous jobs fail
    steps:
      - name: Configure AWS credentials
        uses: aws-actions/configure-aws-credentials@v4
        with:
          role-to-assume: ${{ secrets.AWS_ROLE_ARN }}
          aws-region: ${{ secrets.AWS_REGION }}
          role-session-name: github-runner-session

      - name: Stop EC2 runner
        uses: machulav/ec2-github-runner@v2
        with:
          mode: stop
          github-token: ${{ secrets.GITHUB_TOKEN }}
          label: ${{ needs.start-runner.outputs.label }}
          ec2-instance-id: ${{ needs.start-runner.outputs.ec2-instance-id }}
```