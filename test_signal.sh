#!/bin/bash
# Test Signal notifications setup

set -e

echo "=== Signal Notification Test ==="
echo ""

# Check if Docker is running
if ! docker info > /dev/null 2>&1; then
    echo "❌ Error: Docker is not running"
    echo "Please start Docker and try again"
    exit 1
fi

echo "✓ Docker is running"

# Check if signal-api container exists
if docker ps -a | grep -q ibkr-signal-notifier; then
    echo "✓ Signal API container exists"

    # Check if it's running
    if docker ps | grep -q ibkr-signal-notifier; then
        echo "✓ Signal API container is running"
    else
        echo "⚠ Signal API container is stopped, starting..."
        docker-compose up -d signal-api
        sleep 3
    fi
else
    echo "⚠ Signal API container not found, creating..."
    docker-compose up -d signal-api
    sleep 5
fi

# Check if API is responding
echo ""
echo "Checking Signal API health..."
if curl -s -f http://localhost:8080/v1/about > /dev/null 2>&1; then
    echo "✓ Signal API is responding"
    echo ""
    curl -s http://localhost:8080/v1/about | head -5
else
    echo "❌ Signal API is not responding"
    echo "Check logs with: docker-compose logs signal-api"
    exit 1
fi

echo ""
echo "=== Setup Instructions ==="
echo ""
echo "1. Register your phone number:"
echo "   curl -X POST http://localhost:8080/v1/register/+1234567890"
echo ""
echo "2. Verify with the code you receive via SMS:"
echo "   curl -X POST http://localhost:8080/v1/register/+1234567890/verify/123456"
echo ""
echo "3. Test sending a message:"
echo "   curl -X POST -H 'Content-Type: application/json' \\"
echo "     -d '{\"message\": \"Test\", \"number\": \"+1234567890\", \"recipients\": [\"+0987654321\"]}' \\"
echo "     http://localhost:8080/v2/send"
echo ""
echo "4. Update config.json with your Signal settings"
echo ""
echo "See SIGNAL_SETUP.md for detailed instructions"
