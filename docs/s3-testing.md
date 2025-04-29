# S3 Testing for Memory Collector

This document explains how to set up and run the S3 integration tests for the memory collector.

## Bucket Setup

The tests require two S3 buckets with different authentication methods:

1. **IRSA Bucket (`unvariance-collector-test-irsa`)**: Uses IAM role-based authentication
2. **Key-Auth Bucket (`unvariance-collector-test-key-auth`)**: Uses access key authentication

### Creating the Buckets

Run these commands to create the buckets (adjust region as needed):

```bash
aws s3api create-bucket --bucket unvariance-collector-test-irsa --region us-east-2 --create-bucket-configuration LocationConstraint=us-east-2
aws s3api create-bucket --bucket unvariance-collector-test-key-auth --region us-east-2 --create-bucket-configuration LocationConstraint=us-east-2
```

### Setting Up Lifecycle Policies

To avoid accumulating test data and control costs, set up lifecycle policies to delete objects after 1 day:

```bash
# Create lifecycle policy JSON
cat > lifecycle-policy.json << EOF
{
  "Rules": [
    {
      "ID": "DeleteAfterOneDay",
      "Status": "Enabled",
      "Prefix": "",
      "Expiration": {
        "Days": 1
      }
    }
  ]
}
EOF

# Apply to both buckets
aws s3api put-bucket-lifecycle-configuration --bucket unvariance-collector-test-irsa --lifecycle-configuration file://lifecycle-policy.json
aws s3api put-bucket-lifecycle-configuration --bucket unvariance-collector-test-key-auth --lifecycle-configuration file://lifecycle-policy.json
```

## IAM Permissions Setup

### IRSA Bucket Permissions

The IAM role used by the GitHub Actions runner needs permissions to write to the IRSA bucket. Add this policy to the role:

```json
{
  "Version": "2012-10-17",
  "Statement": [
    {
      "Effect": "Allow",
      "Action": [
        "s3:PutObject",
        "s3:GetObject",
        "s3:ListBucket",
        "s3:DeleteObject",
        "s3:AbortMultipartUpload",
        "s3:ListMultipartUploadParts",
        "s3:ListBucketMultipartUploads"
      ],
      "Resource": [
        "arn:aws:s3:::unvariance-collector-test-irsa",
        "arn:aws:s3:::unvariance-collector-test-irsa/*"
      ]
    }
  ]
}
```

### Access Key Authentication

For the key-auth bucket, create a dedicated IAM user with limited permissions:

1. Create an IAM user (e.g., `collector-test-user`)
2. Attach this policy to the user:

```json
{
  "Version": "2012-10-17",
  "Statement": [
    {
      "Effect": "Allow",
      "Action": [
        "s3:PutObject",
        "s3:GetObject",
        "s3:ListBucket",
        "s3:DeleteObject",
        "s3:AbortMultipartUpload",
        "s3:ListMultipartUploadParts",
        "s3:ListBucketMultipartUploads"
      ],
      "Resource": [
        "arn:aws:s3:::unvariance-collector-test-key-auth",
        "arn:aws:s3:::unvariance-collector-test-key-auth/*"
      ]
    }
  ]
}
```

3. Generate access keys for this user
4. Add the keys to GitHub repository secrets:
   - `S3_ACCESS_KEY_ID`: The access key ID
   - `S3_SECRET_ACCESS_KEY`: The secret access key

## Running the Tests Manually

To run the S3 tests manually:

1. Go to the GitHub repository
2. Navigate to the "Actions" tab
3. Select the "test-ebpf-collector" workflow
4. Click "Run workflow"
5. Use the default settings or customize the EC2 instance type
6. Click "Run workflow"

## Test Workflow Explanation

The S3 test workflow:

1. Creates unique UUIDs for each test run to isolate test data
2. Tests writing to S3 using IAM role authentication
3. Tests writing to S3 using access key authentication
4. Downloads the generated Parquet files
5. Validates the file structure using PQRS
6. Uploads the test files as artifacts for inspection

## Troubleshooting

Common issues and solutions:

- **Permission Denied**: Check IAM role permissions or access key permissions, especially for multi-part upload operations
- **No Files Found**: Verify collector is writing files correctly and paths are correct
- **Invalid Credentials**: Ensure GitHub secrets are set correctly
- **Timeout Issues**: If tests time out, increase the timeout value in the workflow file
- **Incomplete Files**: If files are incomplete, check for multi-part upload permissions or connectivity issues

### Diagnosing Multi-part Upload Issues

If you encounter problems with multi-part uploads:

1. Check S3 permissions include all required multi-part actions (AbortMultipartUpload, etc.)
2. Verify no network interruptions occurred during upload
3. Inspect the S3 bucket for incomplete multi-part uploads:
   ```bash
   aws s3api list-multipart-uploads --bucket your-bucket-name
   ```
4. Enable DEBUG logging in the collector to see detailed upload information

## S3 URI Format

The collector accepts S3 URIs in this format:
```
s3://BUCKET-NAME/PREFIX/
```

Example:
```
s3://unvariance-collector-test-irsa/test-run-123/
``` 